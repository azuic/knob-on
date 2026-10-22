#include "LinuxAddress.hpp"
#include <cassert>
#include <iostream>
#include <sstream>

int main() {
    std::istringstream routes(
        "Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT\n"
        "bad malformed route\n"
        "docker0 00000000 00000000 0001 0 0 0 000000FF 0 0 0\n"
        "down0 00000000 0100000A 0000 0 0 0 00000000 0 0 0\n"
        "reject0 00000000 0100000A 0201 0 0 0 00000000 0 0 0\n"
        "eth0 00000000 0100000A 0003 0 0 200 00000000 0 0 0\n"
        "wlan0 00000000 0101A8C0 0003 0 0 100 00000000 0 0 0\n");
    auto preferred = knob::linuxnet::defaultInterface(routes);
    assert(preferred == "wlan0");
    std::vector<knob::linuxnet::InterfaceAddress> addresses = {
        {"lo", 0x7f000001, true}, {"link", 0xa9fe0101, true},
        {"zero", 0x00000000, true}, {"multicast", 0xe0000001, true},
        {"broadcast", 0xffffffff, true}, {"down0", 0x0a000001, false},
        {"eth0", 0x0a000002, true}, {"wlan0", 0xc0a8012a, true}
    };
    assert(knob::linuxnet::selectAddress(addresses, preferred) == "192.168.1.42");
    assert(knob::linuxnet::selectAddress(addresses, "missing") == "10.0.0.2");
    std::istringstream unavailable;
    assert(knob::linuxnet::defaultInterface(unavailable).empty());
    addresses.resize(6);
    assert(knob::linuxnet::selectAddress(addresses, "").empty());
    assert(knob::linuxnet::selectAddress({}, "").empty());
    std::cout << "IPv4 default-route preference, fallback, inactive/invalid address filtering: passed\n";
}
