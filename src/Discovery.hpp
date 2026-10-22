#pragma once
#include "Socket.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#ifdef _WIN32
#include <iphlpapi.h>
#include "WindowsBonjour.hpp"
#endif
#ifdef __APPLE__
#include <dns_sd.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <sys/select.h>
#endif

namespace knob {
// Bonjour is polled from Rack's UI thread, only when its daemon socket is ready.
// The audio thread never handles discovery or network work.
class Discovery {
#ifdef _WIN32
    net::Runtime runtime;
    WindowsBonjour bonjour;
    WindowsBonjour::Ref service = nullptr;
#elif defined(__APPLE__)
    DNSServiceRef service = nullptr;
#endif
    double nextHostRefresh = 0;
#if defined(__APPLE__) || defined(_WIN32)
    bool registrationFailed = false;
    double nextAttempt = 0;
#ifdef _WIN32
    static void WINAPI registered(WindowsBonjour::Ref, uint32_t, int32_t error,
#else
    static void registered(DNSServiceRef, DNSServiceFlags, DNSServiceErrorType error,
#endif
                           const char* name, const char*, const char*, void* context) {
        auto* self = static_cast<Discovery*>(context);
        if (error != 0) self->registrationFailed = true;
        else if (name) { self->name = name; self->advertised = true; }
    }
#endif
    static std::string localHost() {
#ifdef _WIN32
        // A Windows hostname.local is not guaranteed to resolve on iOS.
        // Show an active LAN IPv4 address even if Bonjour is absent.
        ULONG size = 16384;
        std::vector<unsigned char> buffer(size);
        const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
        ULONG result = ERROR_BUFFER_OVERFLOW;
        for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
            buffer.resize(size);
            result = GetAdaptersAddresses(AF_INET, flags, nullptr,
                reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
        }
        if (result != NO_ERROR) return "";
        std::string fallback;
        for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp ||
                (adapter->IfType != IF_TYPE_ETHERNET_CSMACD && adapter->IfType != IF_TYPE_IEEE80211)) continue;
            for (auto* entry = adapter->FirstUnicastAddress; entry; entry = entry->Next) {
                auto* raw = entry->Address.lpSockaddr;
                if (!raw || raw->sa_family != AF_INET || entry->DadState != IpDadStatePreferred) continue;
                auto* ipv4 = reinterpret_cast<sockaddr_in*>(raw);
                uint32_t ip = ntohl(ipv4->sin_addr.s_addr);
                if (!ip || (ip >> 24) == 127 || (ip >> 16) == 0xa9fe) continue;
                char text[INET_ADDRSTRLEN]{};
                if (!inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text))) continue;
                if (adapter->FirstGatewayAddress) return text;
                if (fallback.empty()) fallback = text;
            }
        }
        return fallback;
#else
#ifdef __APPLE__
        CFStringRef value = SCDynamicStoreCopyLocalHostName(nullptr);
        if (value) {
            char buffer[256]{};
            bool valid = CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8);
            CFRelease(value);
            if (valid && buffer[0]) return std::string(buffer) + ".local";
        }
#endif
        char buffer[256]{};
        if (gethostname(buffer, sizeof(buffer) - 1) != 0 || !buffer[0]) return "";
        std::string host(buffer);
        auto dot = host.find('.');
        if (dot != std::string::npos) host.resize(dot);
        return host + ".local";
#endif
    }
public:
    std::string name;
    std::string address;
    bool advertised = false;
    Discovery() {
        address = localHost();
        // Use the hostname rather than DHCP address for a stable Windows name.
        std::string identity = address;
#ifdef _WIN32
        char hostname[256]{};
        if (runtime.ready() && gethostname(hostname, sizeof(hostname) - 1) == 0) identity = hostname;
#endif
        uint32_t hash = 2166136261u;
        for (unsigned char c : identity) { hash ^= c; hash *= 16777619u; }
        const char alphabet[] = "23456789abcdefghjkmnpqrstuvwxyz";
        name = "knob-";
        for (int i = 0; i < 4; ++i) { name += alphabet[hash % (sizeof(alphabet) - 1)]; hash /= sizeof(alphabet) - 1; }
    }
    ~Discovery() { stop(); }
    Discovery(const Discovery&) = delete;
    Discovery& operator=(const Discovery&) = delete;
    void stop() {
#ifdef __APPLE__
        if (service) DNSServiceRefDeallocate(service);
#elif defined(_WIN32)
        if (service) bonjour.deallocate(service);
#endif
#if defined(__APPLE__) || defined(_WIN32)
        service = nullptr;
        registrationFailed = false;
#endif
        advertised = false;
    }
    void step(bool listening, double now, uint16_t port = 8057) {
        if (!listening) { stop(); return; }
        if (now >= nextHostRefresh) { address = localHost(); nextHostRefresh = now + 5; }
#if defined(__APPLE__) || defined(_WIN32)
#ifdef _WIN32
        if (!runtime.ready() || !bonjour.available()) return;
#endif
        if (registrationFailed) { stop(); nextAttempt = now + 5; }
        if (!service && now >= nextAttempt) {
            nextAttempt = now + 5;
#ifdef _WIN32
            auto result = bonjour.registerService(&service, name.c_str(), port, registered, this);
#else
            auto result = DNSServiceRegister(&service, 0, 0, name.c_str(), "_knobon._tcp", "local.",
                nullptr, htons(port), 0, nullptr, registered, this);
#endif
            if (result != 0) stop();
        }
        if (service) {
#ifdef _WIN32
            int fd = bonjour.socketFD(service);
            if (fd < 0) { stop(); nextAttempt = now + 5; return; }
            net::Socket socket = static_cast<net::Socket>(fd);
#else
            int fd = DNSServiceRefSockFD(service);
            if (fd < 0 || fd >= FD_SETSIZE) { stop(); return; }
            net::Socket socket = fd;
#endif
            fd_set readSet; FD_ZERO(&readSet); FD_SET(socket, &readSet);
            timeval timeout{};
            // Winsock ignores nfds and FD_SETSIZE limits the count, not the handle.
            int ready = select(
#ifdef _WIN32
                0,
#else
                fd + 1,
#endif
                &readSet, nullptr, nullptr, &timeout);
            if (ready < 0) { stop(); nextAttempt = now + 5; }
            else if (ready > 0) {
#ifdef _WIN32
                int result = bonjour.process(service);
#else
                int result = DNSServiceProcessResult(service);
#endif
                if (result != 0) {
                    stop(); nextAttempt = now + 5;
                }
            }
        }
#else
        (void) listening; (void) now; (void) port;
#endif
    }
};
}
