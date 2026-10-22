#pragma once
// Keep Winsock before windows.h (including headers pulled in by Rack).
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace knob {
namespace net {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
class Runtime {
    bool started = false;
public:
    Runtime() { WSADATA data{}; started = WSAStartup(MAKEWORD(2, 2), &data) == 0; }
    ~Runtime() { if (started) WSACleanup(); }
    bool ready() const { return started; }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
};
inline void close(Socket socket) { closesocket(socket); }
inline bool retryable() {
    const int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
}
inline bool configure(Socket socket) {
    u_long nonblocking = 1;
    return ioctlsocket(socket, FIONBIO, &nonblocking) == 0 &&
        SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, 0);
}
#else
using Socket = int;
constexpr Socket invalid = -1;
class Runtime {
public:
    bool ready() const { return true; }
};
inline void close(Socket socket) { ::close(socket); }
inline bool retryable() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
inline bool configure(Socket socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0 ||
        fcntl(socket, F_SETFD, FD_CLOEXEC) < 0) return false;
#ifdef SO_NOSIGPIPE
    int one = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) < 0) return false;
#endif
    return true;
}
#endif
inline bool option(Socket socket, int level, int name, int value) {
    return setsockopt(socket, level, name, reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
}
}
}
