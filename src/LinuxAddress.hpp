#pragma once
// Linux uses a numeric address: a hostname.local does not imply that mDNS exists.
// These POSIX helpers also compile on macOS for address-selection unit checks.
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <cstdint>
#include <fstream>
#include <istream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace knob {
namespace linuxnet {
struct InterfaceAddress {
    std::string interface;
    uint32_t address; // Host byte order.
    bool active;
};

inline std::string defaultInterface(std::istream& routes) {
    std::string line, selected;
    unsigned long bestMetric = std::numeric_limits<unsigned long>::max();
    while (std::getline(routes, line)) {
        std::istringstream fields(line);
        std::string interface;
        unsigned long destination, gateway, flags, references, uses, metric, mask;
        if (!(fields >> interface >> std::hex >> destination >> gateway >> flags >>
              std::dec >> references >> uses >> metric >> std::hex >> mask)) continue;
        // RTF_UP and not RTF_REJECT; prefer the lowest-metric IPv4 default route.
        if (destination || mask || !(flags & 0x1) || (flags & 0x200)) continue;
        if (selected.empty() || metric < bestMetric) {
            selected = interface;
            bestMetric = metric;
        }
    }
    return selected;
}

inline std::string selectAddress(const std::vector<InterfaceAddress>& addresses,
                                 const std::string& preferred) {
    std::string fallback;
    for (const auto& entry : addresses) {
        uint32_t ip = entry.address;
        if (!entry.active || (ip >> 24) == 0 || (ip >> 24) == 127 ||
            (ip >> 16) == 0xa9fe || (ip >> 24) >= 224) continue;
        in_addr raw{};
        raw.s_addr = htonl(ip);
        char text[INET_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET, &raw, text, sizeof(text))) continue;
        if (!preferred.empty() && entry.interface == preferred) return text;
        if (fallback.empty()) fallback = text;
    }
    return fallback;
}

inline std::string localAddress() {
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) return "";
    std::vector<InterfaceAddress> addresses;
    for (auto* entry = interfaces; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || !entry->ifa_name || entry->ifa_addr->sa_family != AF_INET) continue;
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
        bool active = (entry->ifa_flags & IFF_UP) && (entry->ifa_flags & IFF_RUNNING) &&
            !(entry->ifa_flags & IFF_LOOPBACK);
        addresses.push_back({entry->ifa_name, ntohl(ipv4->sin_addr.s_addr), active});
    }
    freeifaddrs(interfaces);
    std::ifstream routes("/proc/net/route");
    return selectAddress(addresses, defaultInterface(routes));
}
}
}
