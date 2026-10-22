#include "TcpServer.hpp"
#include "Discovery.hpp"
#include <cassert>
#include <iostream>
#include <thread>

static void tick(knob::TcpServer& server, int count = 20) {
    for (int i = 0; i < count; ++i) {
        server.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int main() {
    knob::net::Runtime runtime;
    assert(runtime.ready());
    knob::TcpServer server;
    assert(server.start(18057));
    knob::TcpServer conflict;
    assert(!conflict.start(18057));
    assert(conflict.status == "Port 18057 unavailable");
    int connects = 0, disconnects = 0, lines = 0;
    server.onConnect = [&] { ++connects; };
    server.onDisconnect = [&] { ++disconnects; };
    server.onLine = [&](const std::string& line) { assert(line == "hello"); ++lines; };
    auto connectClient = [] {
        auto client = socket(AF_INET, SOCK_STREAM, 0);
        assert(client != knob::net::invalid);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(18057);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        assert(knob::net::configure(client));
        return client;
    };
    auto first = connectClient();
    tick(server);
    assert(server.connected() && connects == 1);
    auto second = connectClient();
    tick(server);
    char data[32]{};
    assert(recv(second, data, sizeof(data), 0) == 0);
    knob::net::close(second);
    assert(connects == 1 && disconnects == 0);
    assert(send(first, "hel", 3, 0) == 3);
    tick(server);
    assert(lines == 0);
    assert(send(first, "lo\n", 3, 0) == 3);
    tick(server);
    assert(lines == 1);
    server.sendLine("feedback");
    tick(server);
    assert(recv(first, data, sizeof(data), 0) == 9);
    assert(std::string(data, 9) == "feedback\n");
    // Slow readers cannot grow the UI's queue without bound.
    server.sendLine(std::string(262144, 'x'));
    assert(!server.connected() && disconnects == 1);
    knob::net::close(first);
    auto reconnected = connectClient();
    tick(server);
    assert(server.connected() && connects == 2);
    knob::net::close(reconnected);
    tick(server);
    assert(!server.connected() && disconnects == 2);
    server.stop();
    assert(conflict.start(18057));
    conflict.stop();
    // Exercise discovery shutdown/retry without requiring a Bonjour daemon.
    knob::Discovery discovery;
    discovery.step(true, knob::now(), 18057);
    discovery.step(false, knob::now(), 18057);
    assert(!discovery.advertised);
    std::cout << "Socket startup, port conflict, single client, framing, feedback, queue bound, reconnect, cleanup: passed\n";
}
