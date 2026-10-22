#pragma once
#include "Socket.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace knob {
inline double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Called only by Rack's UI step. Every socket is nonblocking, with per-frame
// read/write budgets and bounded buffers. Never called from Module::process().
class TcpServer {
    net::Runtime runtime;
    net::Socket listener = net::invalid;
    net::Socket client = net::invalid;
    uint16_t listenerPort = 8057;
    std::string input, output;
    double lastReceive = 0;
    bool announced = false;
public:
    TcpServer() = default;
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    std::string status = "Starting…";
    std::function<void(const std::string&)> onLine;
    std::function<void()> onConnect;
    std::function<void()> onDisconnect;
    ~TcpServer() { stop(); }
    bool connected() const { return client != net::invalid; }
    std::string listeningStatus() const { return "Listening :" + std::to_string(listenerPort); }

    bool start(uint16_t port = 8057) {
        stop();
        listenerPort = port;
        if (!runtime.ready()) { status = "Network startup failed"; return false; }
        listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener == net::invalid) { status = "Socket failed"; return false; }
#ifdef _WIN32
        // SO_REUSEADDR on Windows can steal an existing Bridge's port.
        const int reuseOption = SO_EXCLUSIVEADDRUSE;
#else
        const int reuseOption = SO_REUSEADDR;
#endif
        if (!net::configure(listener) || !net::option(listener, SOL_SOCKET, reuseOption, 1)) {
            net::close(listener); listener = net::invalid;
            status = "Socket setup failed";
            return false;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(listener, 1) < 0) {
            net::close(listener); listener = net::invalid;
            status = "Port " + std::to_string(port) + " unavailable";
            return false;
        }
        status = listeningStatus();
        return true;
    }

    void drop() {
        if (connected()) net::close(client);
        client = net::invalid;
        input.clear(); output.clear();
        if (announced && onDisconnect) onDisconnect();
        announced = false;
        if (listener != net::invalid) status = listeningStatus();
    }

    void stop() {
        drop();
        if (listener != net::invalid) net::close(listener);
        listener = net::invalid;
    }

    void sendLine(const std::string& line) {
        if (!connected()) return;
        if (output.size() + line.size() + 1 > 262144) { drop(); return; }
        output += line;
        output += '\n';
    }

    void step() {
        if (listener == net::invalid) return;
        // Keep the existing controller when a second client tries to connect.
        net::Socket incoming = accept(listener, nullptr, nullptr);
        if (incoming != net::invalid) {
            if (connected() || !net::configure(incoming)) net::close(incoming);
            else {
                client = incoming;
                net::option(client, IPPROTO_TCP, TCP_NODELAY, 1);
                lastReceive = now();
                announced = true;
                status = "Connected";
                if (onConnect) onConnect();
            }
        }
        if (!connected()) return;
        char buffer[4096];
        for (int budget = 0; budget < 4 && connected(); ++budget) {
            auto count = recv(client, buffer, sizeof(buffer), 0);
            if (count == 0) { drop(); return; }
            if (count < 0) {
                if (!net::retryable()) drop();
                break;
            }
            lastReceive = now();
            input.append(buffer, static_cast<size_t>(count));
            // Both an unterminated frame and an oversized terminated frame fail.
            if (input.size() > 65536) { drop(); return; }
        }
        // Limit parsed commands too: tiny frames must not monopolize the UI.
        for (int budget = 0; budget < 64 && connected(); ++budget) {
            auto end = input.find('\n');
            if (end == std::string::npos) break;
            auto line = input.substr(0, end);
            input.erase(0, end + 1);
            if (!line.empty() && onLine) onLine(line);
        }
        if (!connected()) return;
        if (now() - lastReceive > 8) { drop(); return; }
        if (!output.empty()) {
#ifdef MSG_NOSIGNAL
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            auto sent = ::send(client, output.data(), static_cast<int>(std::min<size_t>(output.size(), 16384)), flags);
            if (sent > 0) output.erase(0, static_cast<size_t>(sent));
            else if (sent < 0 && !net::retryable()) drop();
        }
    }
};
}
