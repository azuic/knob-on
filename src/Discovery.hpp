#pragma once
#include <algorithm>
#include <arpa/inet.h>
#include <cstdint>
#include <string>
#include <unistd.h>
#ifdef __APPLE__
#include <dns_sd.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <sys/select.h>
#endif

namespace knob {
// Bonjour is polled from Rack's UI thread, only when its daemon socket is ready.
// The audio thread never handles discovery or network work.
class Discovery {
#ifdef __APPLE__
    DNSServiceRef service = nullptr;
    bool registrationFailed = false;
    double nextAttempt = 0;
    double nextHostRefresh = 0;
    static void registered(DNSServiceRef, DNSServiceFlags, DNSServiceErrorType error,
                           const char* name, const char*, const char*, void* context) {
        auto* self = static_cast<Discovery*>(context);
        if (error != kDNSServiceErr_NoError) self->registrationFailed = true;
        else if (name) { self->name = name; self->advertised = true; }
    }
#endif
    static std::string localHost() {
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
    }
public:
    std::string name;
    std::string address;
    bool advertised = false;
    Discovery() {
        address = localHost();
        // Stable across Rack restarts and patch loads on this Mac.
        uint32_t hash = 2166136261u;
        for (unsigned char c : address) { hash ^= c; hash *= 16777619u; }
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
        service = nullptr;
        registrationFailed = false;
#endif
        advertised = false;
    }
    void step(bool listening, double now, uint16_t port = 8057) {
#ifdef __APPLE__
        if (!listening) { stop(); return; }
        if (now >= nextHostRefresh) { address = localHost(); nextHostRefresh = now + 5; }
        if (registrationFailed) { stop(); nextAttempt = now + 5; }
        if (!service && now >= nextAttempt) {
            nextAttempt = now + 5;
            auto result = DNSServiceRegister(&service, 0, 0, name.c_str(), "_knobon._tcp", "local.",
                nullptr, htons(port), 0, nullptr, registered, this);
            if (result != kDNSServiceErr_NoError) stop();
        }
        if (service) {
            int fd = DNSServiceRefSockFD(service);
            if (fd < 0 || fd >= FD_SETSIZE) { stop(); return; }
            fd_set readSet; FD_ZERO(&readSet); FD_SET(fd, &readSet);
            timeval timeout{};
            if (select(fd + 1, &readSet, nullptr, nullptr, &timeout) > 0) {
                if (DNSServiceProcessResult(service) != kDNSServiceErr_NoError) {
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
