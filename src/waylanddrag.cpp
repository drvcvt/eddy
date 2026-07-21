#include "waylanddrag.h"
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QUrl>
#include <QWidget>
#include <QWindow>
#include <QPixmap>
#include <QImage>

#if defined(EDDY_HAVE_WAYLAND_CLIENT)
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#endif

namespace eddy {

#if defined(EDDY_HAVE_WAYLAND_CLIENT)
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
    wl_compositor *compositor = nullptr;
    uint32_t compositorVersion = 0;
    wl_shm *shm = nullptr;
};

struct CachedGlobals {
    wl_display *display = nullptr;
    wl_data_device_manager *manager = nullptr;
    uint32_t version = 0;
    wl_compositor *compositor = nullptr;
    wl_shm *shm = nullptr;
};

void registryGlobal(void *data, wl_registry *registry, uint32_t name,
                    const char *interface, uint32_t version) {
    auto *state = static_cast<RegistryState *>(data);
    if (std::strcmp(interface, wl_data_device_manager_interface.name) == 0 && !state->manager) {
        state->managerVersion = std::min<uint32_t>(version, 3);
        state->manager = static_cast<wl_data_device_manager *>(
            wl_registry_bind(registry, name, &wl_data_device_manager_interface, state->managerVersion));
    } else if (std::strcmp(interface, wl_compositor_interface.name) == 0 && !state->compositor) {
        state->compositorVersion = std::min<uint32_t>(version, 4);
        state->compositor = static_cast<wl_compositor *>(
            wl_registry_bind(registry, name, &wl_compositor_interface, state->compositorVersion));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0 && !state->shm) {
        state->shm = static_cast<wl_shm *>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
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
    wl_surface *icon = nullptr;
    wl_buffer *iconBuffer = nullptr;
    void *iconData = nullptr;
    qsizetype iconDataSize = 0;
    int iconFd = -1;
    int iconWidth = 0;
    int iconHeight = 0;
};

void destroyDrag(NativeDrag *drag) {
    if (!drag)
        return;
    if (drag->source)
        wl_data_source_destroy(drag->source);
    if (drag->device)
        wl_data_device_destroy(drag->device);
    if (drag->iconBuffer)
        wl_buffer_destroy(drag->iconBuffer);
    if (drag->icon)
        wl_surface_destroy(drag->icon);
    if (drag->iconData && drag->iconDataSize > 0)
        ::munmap(drag->iconData, size_t(drag->iconDataSize));
    if (drag->iconFd >= 0)
        ::close(drag->iconFd);
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

CachedGlobals &cachedGlobals() {
    static CachedGlobals cache;
    return cache;
}

CachedGlobals *globalsFor(wl_display *display) {
    auto &cache = cachedGlobals();
    if (cache.display == display && cache.manager) {
        return &cache;
    }

    if (cache.manager) {
        wl_data_device_manager_destroy(cache.manager);
        if (cache.compositor)
            wl_compositor_destroy(cache.compositor);
        if (cache.shm)
            wl_shm_destroy(cache.shm);
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
    cache.compositor = registryState.compositor;
    cache.shm = registryState.shm;
    return &cache;
}

int createAnonymousFile(qsizetype size) {
    const int fd = ::memfd_create("eddy-drag-icon", MFD_CLOEXEC);
    if (fd < 0)
        return -1;
    if (::ftruncate(fd, off_t(size)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void prepareDragIcon(NativeDrag *drag, wl_compositor *compositor, wl_shm *shm, const QPixmap &ghost) {
    if (!drag || !compositor || !shm || ghost.isNull())
        return;

    QImage img = ghost.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (img.isNull())
        return;

    const int width = img.width();
    const int height = img.height();
    const int stride = width * 4;
    const qsizetype size = qsizetype(stride) * height;
    const int fd = createAnonymousFile(size);
    if (fd < 0)
        return;

    void *data = ::mmap(nullptr, size_t(size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        ::close(fd);
        return;
    }

    auto *pool = wl_shm_create_pool(shm, fd, int(size));
    if (!pool) {
        ::munmap(data, size_t(size));
        ::close(fd);
        return;
    }

    auto *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    if (!buffer) {
        ::munmap(data, size_t(size));
        ::close(fd);
        return;
    }

    auto *surface = wl_compositor_create_surface(compositor);
    if (!surface) {
        wl_buffer_destroy(buffer);
        ::munmap(data, size_t(size));
        ::close(fd);
        return;
    }

    auto *dst = static_cast<uchar *>(data);
    for (int y = 0; y < height; ++y)
        std::memcpy(dst + y * stride, img.constScanLine(y), size_t(stride));

    drag->icon = surface;
    drag->iconBuffer = buffer;
    drag->iconData = data;
    drag->iconDataSize = size;
    drag->iconFd = fd;
    drag->iconWidth = width;
    drag->iconHeight = height;
}

void commitDragIcon(NativeDrag *drag) {
    if (!drag || !drag->icon || !drag->iconBuffer)
        return;
    wl_surface_attach(drag->icon, drag->iconBuffer, 0, 0);
    wl_surface_damage(drag->icon, 0, 0, drag->iconWidth, drag->iconHeight);
    wl_surface_commit(drag->icon);
}

} // namespace
#endif

bool startWaylandFileDrag(QWidget *origin, const QString &path, const QStringList &mimeTypes,
                          const QPixmap &ghost) {
#if defined(EDDY_HAVE_WAYLAND_CLIENT)
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

    CachedGlobals *globals = globalsFor(display);
    if (!globals || !globals->manager)
        return false;

    auto *drag = new NativeDrag;
    drag->path = canonicalOrAbsolute(path);
    drag->source = wl_data_device_manager_create_data_source(globals->manager);
    drag->device = wl_data_device_manager_get_data_device(globals->manager, seat);
    if (!drag->source || !drag->device) {
        destroyDrag(drag);
        return false;
    }
    prepareDragIcon(drag, globals->compositor, globals->shm, ghost);

    for (const QString &mime : mimeTypes) {
        if (!mime.isEmpty())
            wl_data_source_offer(drag->source, mime.toUtf8().constData());
    }
    wl_data_source_add_listener(drag->source, &sourceListener, drag);
    if (globals->version >= 3)
        wl_data_source_set_actions(drag->source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    wl_data_device_start_drag(drag->device, drag->source, originSurface, drag->icon, serial);
    commitDragIcon(drag);
    wl_display_flush(display);
    return true;
#else
    Q_UNUSED(origin);
    Q_UNUSED(path);
    Q_UNUSED(mimeTypes);
    Q_UNUSED(ghost);
    return false;
#endif
}

}
