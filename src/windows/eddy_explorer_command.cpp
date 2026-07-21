#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <atomic>
#include <new>
#include <string>

namespace {

constexpr CLSID eddyCommandClsid = {
    0x9d42e9a3, 0x9c3b, 0x4e14, {0xa3, 0xed, 0x92, 0xd8, 0x51, 0x67, 0xd2, 0x35}
};

HMODULE moduleHandle = nullptr;
std::atomic<long> objectCount = 0;

bool isSupportedImage(const wchar_t *path) {
    const wchar_t *extension = PathFindExtensionW(path);
    if (!extension || !*extension)
        return false;

    constexpr const wchar_t *extensions[] = {
        L".png", L".jpg", L".jpeg", L".bmp", L".gif",
        L".webp", L".tif", L".tiff"
    };
    for (const wchar_t *supported : extensions) {
        if (_wcsicmp(extension, supported) == 0)
            return true;
    }
    return false;
}

HRESULT selectedImagePath(IShellItemArray *items, PWSTR *path) {
    if (!items || !path)
        return E_INVALIDARG;

    DWORD count = 0;
    HRESULT result = items->GetCount(&count);
    if (FAILED(result) || count != 1)
        return S_FALSE;

    IShellItem *item = nullptr;
    result = items->GetItemAt(0, &item);
    if (FAILED(result))
        return result;
    result = item->GetDisplayName(SIGDN_FILESYSPATH, path);
    item->Release();
    return result;
}

std::wstring installedPath(const wchar_t *fileName) {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(moduleHandle, path, ARRAYSIZE(path));
    if (!length || length == ARRAYSIZE(path))
        return {};
    if (!PathRemoveFileSpecW(path))
        return {};
    if (!PathAppendW(path, fileName))
        return {};
    return path;
}

class EddyExplorerCommand final : public IExplorerCommand {
public:
    EddyExplorerCommand() { ++objectCount; }

    IFACEMETHODIMP QueryInterface(REFIID interfaceId, void **object) override {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (interfaceId == IID_IUnknown || interfaceId == __uuidof(IExplorerCommand))
            *object = static_cast<IExplorerCommand *>(this);
        if (!*object)
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return ++references; }

    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = --references;
        if (!remaining)
            delete this;
        return remaining;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray *, PWSTR *title) override {
        return SHStrDupW(L"Open with Eddy", title);
    }

    IFACEMETHODIMP GetIcon(IShellItemArray *, PWSTR *icon) override {
        const std::wstring executable = installedPath(L"eddy.exe");
        if (executable.empty())
            return E_FAIL;
        return SHStrDupW((executable + L",0").c_str(), icon);
    }

    IFACEMETHODIMP GetToolTip(IShellItemArray *, PWSTR *toolTip) override {
        if (toolTip)
            *toolTip = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCanonicalName(GUID *canonicalName) override {
        if (!canonicalName)
            return E_POINTER;
        *canonicalName = eddyCommandClsid;
        return S_OK;
    }

    IFACEMETHODIMP GetState(IShellItemArray *items, BOOL, EXPCMDSTATE *state) override {
        if (!state)
            return E_POINTER;
        *state = ECS_HIDDEN;
        PWSTR path = nullptr;
        const HRESULT result = selectedImagePath(items, &path);
        if (SUCCEEDED(result) && path && isSupportedImage(path))
            *state = ECS_ENABLED;
        CoTaskMemFree(path);
        return S_OK;
    }

    IFACEMETHODIMP Invoke(IShellItemArray *items, IBindCtx *) override {
        PWSTR path = nullptr;
        const HRESULT result = selectedImagePath(items, &path);
        if (FAILED(result) || !path)
            return FAILED(result) ? result : E_INVALIDARG;

        const std::wstring executable = installedPath(L"eddy.exe");
        const std::wstring arguments = L"-f \"" + std::wstring(path) + L"\"";
        CoTaskMemFree(path);
        if (executable.empty())
            return E_FAIL;

        const HINSTANCE launched = ShellExecuteW(
            nullptr, L"open", executable.c_str(), arguments.c_str(), nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(launched) <= 32)
            return HRESULT_FROM_WIN32(static_cast<DWORD>(reinterpret_cast<INT_PTR>(launched)));
        return S_OK;
    }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS *flags) override {
        if (!flags)
            return E_POINTER;
        *flags = ECF_DEFAULT;
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand **commands) override {
        if (commands)
            *commands = nullptr;
        return E_NOTIMPL;
    }

private:
    ~EddyExplorerCommand() { --objectCount; }
    std::atomic<ULONG> references{1};
};

class EddyClassFactory final : public IClassFactory {
public:
    EddyClassFactory() { ++objectCount; }

    IFACEMETHODIMP QueryInterface(REFIID interfaceId, void **object) override {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (interfaceId == IID_IUnknown || interfaceId == IID_IClassFactory)
            *object = static_cast<IClassFactory *>(this);
        if (!*object)
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return ++references; }

    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = --references;
        if (!remaining)
            delete this;
        return remaining;
    }

    IFACEMETHODIMP CreateInstance(IUnknown *outer, REFIID interfaceId, void **object) override {
        if (outer)
            return CLASS_E_NOAGGREGATION;
        auto *command = new (std::nothrow) EddyExplorerCommand;
        if (!command)
            return E_OUTOFMEMORY;
        const HRESULT result = command->QueryInterface(interfaceId, object);
        command->Release();
        return result;
    }

    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock)
            ++objectCount;
        else
            --objectCount;
        return S_OK;
    }

private:
    ~EddyClassFactory() { --objectCount; }
    std::atomic<ULONG> references{1};
};

}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        moduleHandle = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(
    REFCLSID classId, REFIID interfaceId, void **object) {
    if (classId != eddyCommandClsid)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto *factory = new (std::nothrow) EddyClassFactory;
    if (!factory)
        return E_OUTOFMEMORY;
    const HRESULT result = factory->QueryInterface(interfaceId, object);
    factory->Release();
    return result;
}

STDAPI DllCanUnloadNow() {
    return objectCount == 0 ? S_OK : S_FALSE;
}
