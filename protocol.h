#pragma once

#include <cstdint>
#include <vector>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace proto {

static constexpr uint16_t DEFAULT_PORT = 54000;
static constexpr const char* DEFAULT_HOST = "127.0.0.1";

// Команди прикладного рівня.
enum Command : uint32_t {
    CMD_SET_SIZE = 1,   // payload: uint32 N, uint32 num_threads
    CMD_SEND_DATA = 2,  // payload: uint32 count, int32[count]
    CMD_START = 3,      // payload: none; response: uint32 CMD_START, uint32 ok(1=success,0=error)
    CMD_STATUS = 4,     // payload: none (request), response: uint32 ready (0/1)
    CMD_GET_RESULT = 5  // payload: none, response: uint32 count, int32[count]
};

inline bool send_all(SOCKET sock, const char* data, int len) {
    int sent_total = 0;
    while (sent_total < len) {
        int sent = send(sock, data + sent_total, len - sent_total, 0);
        if (sent == SOCKET_ERROR || sent == 0) {
            return false;
        }
        sent_total += sent;
    }
    return true;
}

inline bool recv_all(SOCKET sock, char* data, int len) {
    int recv_total = 0;
    while (recv_total < len) {
        int received = recv(sock, data + recv_total, len - recv_total, 0);
        if (received == SOCKET_ERROR || received == 0) {
            return false;
        }
        recv_total += received;
    }
    return true;
}

inline bool send_u32(SOCKET sock, uint32_t value) {
    uint32_t net = htonl(value);
    return send_all(sock, reinterpret_cast<const char*>(&net), static_cast<int>(sizeof(net)));
}

inline bool recv_u32(SOCKET sock, uint32_t& value) {
    uint32_t net = 0;
    if (!recv_all(sock, reinterpret_cast<char*>(&net), static_cast<int>(sizeof(net)))) {
        return false;
    }
    value = ntohl(net);
    return true;
}

inline bool send_i32(SOCKET sock, int32_t value) {
    uint32_t net = htonl(static_cast<uint32_t>(value));
    return send_all(sock, reinterpret_cast<const char*>(&net), static_cast<int>(sizeof(net)));
}

inline bool recv_i32(SOCKET sock, int32_t& value) {
    uint32_t net = 0;
    if (!recv_all(sock, reinterpret_cast<char*>(&net), static_cast<int>(sizeof(net)))) {
        return false;
    }
    value = static_cast<int32_t>(ntohl(net));
    return true;
}

inline bool send_i32_vector(SOCKET sock, const std::vector<int32_t>& values) {
    if (!send_u32(sock, static_cast<uint32_t>(values.size()))) {
        return false;
    }
    for (int32_t v : values) {
        if (!send_i32(sock, v)) {
            return false;
        }
    }
    return true;
}

inline bool recv_i32_vector(SOCKET sock, std::vector<int32_t>& values) {
    uint32_t count = 0;
    if (!recv_u32(sock, count)) {
        return false;
    }

    constexpr uint32_t MAX_ELEMENTS = 1024u * 1024u;
    if (count > MAX_ELEMENTS) {
        return false;
    }
    values.assign(count, 0);
    for (uint32_t i = 0; i < count; ++i) {
        if (!recv_i32(sock, values[i])) {
            return false;
        }
    }
    return true;
}

inline void close_socket(SOCKET sock) {
    closesocket(sock);
}

}
