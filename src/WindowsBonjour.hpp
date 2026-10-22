#pragma once
#include "Socket.hpp"
#include <cstdint>
#include <cstring>
#include <string>

#ifdef _WIN32
namespace knob {
// Optional Bonjour ABI, as documented by Apple's mDNSResponder dns_sd.h.
// No Bonjour SDK, import library, daemon, or DLL is bundled with the plugin.
class WindowsBonjour {
public:
    using Ref = void*;
    using Callback = void (WINAPI *)(Ref, uint32_t, int32_t, const char*, const char*, const char*, void*);
private:
    HMODULE library = nullptr;
    using Register = int32_t (WINAPI *)(Ref*, uint32_t, uint32_t, const char*, const char*, const char*,
        const char*, uint16_t, uint16_t, const void*, Callback, void*);
    using Deallocate = void (WINAPI *)(Ref);
    using SockFD = int (WINAPI *)(Ref);
    using Process = int32_t (WINAPI *)(Ref);
    Register registerFunction = nullptr;
    Deallocate deallocateFunction = nullptr;
    SockFD socketFunction = nullptr;
    Process processFunction = nullptr;
    template <typename Function> Function symbol(const char* name) {
        FARPROC address = GetProcAddress(library, name);
        Function function = nullptr;
        static_assert(sizeof(function) == sizeof(address), "Windows function pointer ABI");
        std::memcpy(&function, &address, sizeof(function));
        return function;
    }
public:
    WindowsBonjour() {
        wchar_t directory[MAX_PATH]{};
        UINT length = GetSystemDirectoryW(directory, MAX_PATH);
        if (!length || length >= MAX_PATH) return;
        std::wstring path = std::wstring(directory) + L"\\dnssd.dll";
        // Never search the patch, plugin, or working directory for executable code.
        library = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) return;
        registerFunction = symbol<Register>("DNSServiceRegister");
        deallocateFunction = symbol<Deallocate>("DNSServiceRefDeallocate");
        socketFunction = symbol<SockFD>("DNSServiceRefSockFD");
        processFunction = symbol<Process>("DNSServiceProcessResult");
        if (!registerFunction || !deallocateFunction || !socketFunction || !processFunction) {
            FreeLibrary(library); library = nullptr;
        }
    }
    ~WindowsBonjour() { if (library) FreeLibrary(library); }
    WindowsBonjour(const WindowsBonjour&) = delete;
    WindowsBonjour& operator=(const WindowsBonjour&) = delete;
    bool available() const { return library != nullptr; }
    int32_t registerService(Ref* ref, const char* name, uint16_t port, Callback callback, void* context) {
        return registerFunction(ref, 0, 0, name, "_knobon._tcp", "local.", nullptr,
            htons(port), 0, nullptr, callback, context);
    }
    void deallocate(Ref ref) { deallocateFunction(ref); }
    int socketFD(Ref ref) { return socketFunction(ref); }
    int32_t process(Ref ref) { return processFunction(ref); }
};
}
#endif
