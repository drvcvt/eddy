#include "waylanddrag.h"
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QUrl>
#include <QWidget>
#include <QWindow>

#if defined(EDDY_HAVE_WAYLAND_CLIENT) && QT_CONFIG(wayland)
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <thread>
#include <unistd.h>
#endif

namespace eddy {

#if defined(EDDY_HAVE_WAYLAND_CLIENT) && QT_CONFIG(wayland)
namespace {

QString canonicalOrAbsolute(const QString &path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool writeAll(int fd, const char *data, qsizetype size) {
    qsizetype off = 0;
    while (off < size) {
        const ssize_t n = ::write(fd, data + off, size_t(size - off));
        if (n > 0) {
            off += n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
}

void sendFilePayload(QString path, QByteArray mime, int fd) {
    std::thread([path = std::move(path), mime = std::move(mime), fd] {
        const auto closeFd = qScopeGuard([fd] { ::close(fd); });
        if (mime == QByteArrayLiteral("text/uri-list")) {
            const QByteArray uri = QUrl::fromLocalFile(canonicalOrAbsolute(path)).toEncoded()
                + QByteArrayLiteral("\r\n");
            writeAll(fd, uri.constData(), uri.size());
            return;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;
        while (!file.atEnd()) {
            const QByteArray chunk = file.read(64 * 1024);
            if (chunk.isEmpty())
                break;
            if (!writeAll(fd, chunk.constData(), chunk.size()))
                break;
        }
    }).detach();
}

struct RegistryState {
    wl_data_device_manager *manager = nullptr;
    uint32_t managerVersion = 0;
};

struct CachedManager {
    wl_display *display = nullptr;
    wl_data_device_manager *manager = nullptr;
    uint32_t version = 0;
};

void registryGlobal(void *data, wl_registry *registry, uint32_t name,
                    const char *interface, uint32_t version) {
    auto *state = static_cast<RegistryState *>(data);
    if (std::strcmp(interface, wl_data_device_manager_interface.name) == 0 && !state->manager) {
        state->managerVersion = std::min<uint32_t>(version, 3);
        state->manager = static_cast<wl_data_device_manager *>(
            wl_registry_bind(registry, name, &wl_data_device_manager_interface, state->managerVersion));
    }
}

void registryRemove(void *, wl_registry *, uint32_t) {}

const wl_registry_listener registryListener = {
    registryGlobal,
    registryRemove,
};

struct NativeDrag {
    QString path;
    wl_data_source *source = nullptr;
    wl_data_device *device = nullptr;
};

void destroyDrag(NativeDrag *drag) {
    if (!drag)
        return;
    if (drag->source)
        wl_data_source_destroy(drag->source);
    if (drag->device)
        wl_data_device_destroy(drag->device);
    delete drag;
}

void sourceTarget(void *, wl_data_source *, const char *) {}

void sourceSend(void *data, wl_data_source *, const char *mimeType, int32_t fd) {
    auto *drag = static_cast<NativeDrag *>(data);
    sendFilePayload(drag->path, QByteArray(mimeType), fd);
}

void sourceCancelled(void *data, wl_data_source *) {
    destroyDrag(static_cast<NativeDrag *>(data));
}

void sourceDropPerformed(void *, wl_data_source *) {}

void sourceFinished(void *data, wl_data_source *) {
    destroyDrag(static_cast<NativeDrag *>(data));
}

void sourceAction(void *, wl_data_source *, uint32_t) {}

const wl_data_source_listener sourceListener = {
    sourceTarget,
    sourceSend,
    sourceCancelled,
    sourceDropPerformed,
    sourceFinished,
    sourceAction,
};

wl_surface *windowSurface(QWidget *origin) {
    if (!origin)
        return nullptr;
    QWindow *window = origin->windowHandle();
    if (!window) {
        origin->winId();
        window = origin->windowHandle();
    }
    if (!window)
        return nullptr;
    auto *native = QGuiApplication::platformNativeInterface();
    if (!native)
        return nullptr;
    if (void *surface = native->nativeResourceForWindow(QByteArrayLiteral("surface"), window))
        return static_cast<wl_surface *>(surface);
    if (void *surface = native->nativeResourceForWindow(QByteArrayLiteral("wl_surface"), window))
        return static_cast<wl_surface *>(surface);
    return nullptr;
}

CachedManager &cachedManager() {
    static CachedManager cache;
    return cache;
}

wl_data_device_manager *dataDeviceManagerFor(wl_display *display, uint32_t *versionOut) {
    auto &cache = cachedManager();
    if (cache.display == display && cache.manager) {
        if (versionOut)
            *versionOut = cache.version;
        return cache.manager;
    }

    if (cache.manager) {
        wl_data_device_manager_destroy(cache.manager);
        cache = {};
    }

    RegistryState registryState;
    wl_registry *registry = wl_display_get_registry(display);
    if (!registry)
        return nullptr;
    wl_registry_add_listener(registry, &registryListener, &registryState);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    if (!registryState.manager)
        return nullptr;

    cache.display = display;
    cache.manager = registryState.manager;
    cache.version = registryState.managerVersion;
    if (versionOut)
        *versionOut = cache.version;
    return cache.manager;
}

} // namespace
#endif

bool startWaylandFileDrag(QWidget *origin, const QString &path, const QStringList &mimeTypes) {
#if defined(EDDY_HAVE_WAYLAND_CLIENT) && QT_CONFIG(wayland)
    if (path.isEmpty() || mimeTypes.isEmpty())
        return false;
    auto *waylandApp = qGuiApp
        ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()
        : nullptr;
    if (!waylandApp)
        return false;

    wl_display *display = waylandApp->display();
    wl_seat *seat = waylandApp->lastInputSeat();
    if (!seat)
        seat = waylandApp->seat();
    wl_surface *originSurface = windowSurface(origin);
    const uint serial = waylandApp->lastInputSerial();
    if (!display || !seat || !originSurface || serial == 0)
        return false;

    uint32_t managerVersion = 0;
    wl_data_device_manager *manager = dataDeviceManagerFor(display, &managerVersion);
    if (!manager)
        return false;

    auto *drag = new NativeDrag;
    drag->path = canonicalOrAbsolute(path);
    drag->source = wl_data_device_manager_create_data_source(manager);
    drag->device = wl_data_device_manager_get_data_device(manager, seat);
    if (!drag->source || !drag->device) {
        destroyDrag(drag);
        return false;
    }

    for (const QString &mime : mimeTypes) {
        if (!mime.isEmpty())
            wl_data_source_offer(drag->source, mime.toUtf8().constData());
    }
    wl_data_source_add_listener(drag->source, &sourceListener, drag);
    if (managerVersion >= 3)
        wl_data_source_set_actions(drag->source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    wl_data_device_start_drag(drag->device, drag->source, originSurface, nullptr, serial);
    wl_display_flush(display);
    return true;
#else
    Q_UNUSED(origin);
    Q_UNUSED(path);
    Q_UNUSED(mimeTypes);
    return false;
#endif
}

}
