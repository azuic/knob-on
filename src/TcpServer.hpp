#pragma once
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <functional>
#include <string>

namespace knob {
inline double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Called only by Rack's UI step. Every socket is nonblocking, with per-frame
// read/write budgets and bounded buffers. Never called from Module::process().
class TcpServer {
    int listener = -1;
    int client = -1;
    std::string input, output;
    double lastReceive = 0;
    bool announced = false;
public:
    std::string status = "Starting…";
    std::function<void(const std::string&)> onLine;
    std::function<void()> onConnect;
    std::function<void()> onDisconnect;
    ~TcpServer() { stop(); }
    bool connected() const { return client >= 0; }

    static void configure(int fd) {
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    }

    bool start(uint16_t port = 8057) {
        stop();
        listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) { status = "Socket failed"; return false; }
        configure(listener);
        int one = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(listener, 1) < 0) {
            close(listener); listener = -1;
            status = "Port 8057 unavailable";
            return false;
        }
        status = "Listening :8057";
        return true;
    }

    void drop() {
        if (client >= 0) close(client);
        client = -1;
        input.clear(); output.clear();
        if (announced && onDisconnect) onDisconnect();
        announced = false;
        if (listener >= 0) status = "Listening :8057";
    }

    void stop() {
        drop();
        if (listener >= 0) close(listener);
        listener = -1;
    }

    void sendLine(const std::string& line) {
        if (!connected()) return;
        if (output.size() + line.size() + 1 > 262144) { drop(); return; }
        output += line;
        output += '\n';
    }

    void step() {
        if (listener < 0) return;
        // Keep the existing controller when a second client tries to connect.
        int incoming = accept(listener, nullptr, nullptr);
        if (incoming >= 0) {
            if (client >= 0) close(incoming);
            else {
                client = incoming;
                configure(client);
                int one = 1;
                setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                lastReceive = now();
                announced = true;
                status = "Connected";
                if (onConnect) onConnect();
            }
        }
        if (!connected()) return;
        char buffer[4096];
        for (int budget = 0; budget < 4 && connected(); ++budget) {
            ssize_t count = recv(client, buffer, sizeof(buffer), 0);
            if (count == 0) { drop(); return; }
            if (count < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) drop();
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
            ssize_t sent = ::send(client, output.data(), std::min<size_t>(output.size(), 16384), flags);
            if (sent > 0) output.erase(0, static_cast<size_t>(sent));
            else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) drop();
        }
    }
};
}
