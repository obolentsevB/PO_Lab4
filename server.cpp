#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <iostream>
#include <thread>
#include <vector>
#include <stdexcept>

#include "protocol.h"

namespace {

struct WinSockContext {
    WinSockContext() {
        WSADATA wsa_data{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }

    ~WinSockContext() {
        WSACleanup();
    }
};

struct ClientSession {
    uint32_t n = 0;
    uint32_t num_threads = 1;
    std::vector<int32_t> matrix;
    std::vector<int32_t> result;
    bool has_matrix = false;
    bool ready = false;
};

std::vector<int32_t> compute_variant_10(const std::vector<int32_t>& matrix, uint32_t n, uint32_t num_threads) {
    std::vector<int32_t> out = matrix;

    if (n == 0) return out;
    if (num_threads == 0) num_threads = 1;
    if (num_threads > n) num_threads = n;

    // Розподіляємо стовпці між потоками.
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const uint32_t cols_per_thread = n / num_threads;
    const uint32_t remainder = n % num_threads;
    uint32_t start_col = 0;

    for (uint32_t t = 0; t < num_threads; ++t) {
        uint32_t end_col = start_col + cols_per_thread + (t < remainder ? 1u : 0u);
        threads.emplace_back([&out, &matrix, n, start_col, end_col]() {
            for (uint32_t j = start_col; j < end_col; ++j) {
                int64_t col_sum = 0;
                for (uint32_t i = 0; i < n; ++i) {
                    col_sum += matrix[i * n + j];
                }
                out[j * n + j] = static_cast<int32_t>(col_sum);
            }
        });
        start_col = end_col;
    }

    for (auto& thr : threads) {
        thr.join();
    }

    return out;
}

void handle_client(SOCKET client_sock, sockaddr_in client_addr) {
    char ip_buf[INET_ADDRSTRLEN] = {};
    const char* ip_str = inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
    if (ip_str == nullptr) ip_str = "unknown";
    std::cout << "[+] Client connected: " << ip_str << ":" << ntohs(client_addr.sin_port) << "\n";

    ClientSession session;

    try {
        while (true) {
            uint32_t cmd = 0;
            if (!proto::recv_u32(client_sock, cmd)) {
                break;
            }

            if (cmd == proto::CMD_SET_SIZE) {
                uint32_t n = 0;
                if (!proto::recv_u32(client_sock, n)) {
                    break;
                }

                uint32_t num_threads = 0;
                if (!proto::recv_u32(client_sock, num_threads)) {
                    break;
                }

                if (n == 0 || n > 1024) {
                    std::cerr << "[!] Invalid matrix size from client: " << n << "\n";
                    session = ClientSession{};
                    continue;
                }

                if (num_threads == 0 || num_threads > 64) {
                    std::cerr << "[!] Invalid num_threads: " << num_threads << ", clamping to 1\n";
                    num_threads = 1;
                }

                session.n = n;
                session.num_threads = num_threads;
                session.matrix.clear();
                session.result.clear();
                session.has_matrix = false;
                session.ready = false;
            } else if (cmd == proto::CMD_SEND_DATA) {
                std::vector<int32_t> data;
                if (!proto::recv_i32_vector(client_sock, data)) {
                    break;
                }

                if (session.n == 0) {
                    std::cerr << "[!] SEND_DATA before SET_SIZE\n";
                    continue;
                }

                const uint64_t expected = static_cast<uint64_t>(session.n) * session.n;
                if (data.size() != expected) {
                    std::cerr << "[!] Invalid data length: got " << data.size() << ", expected " << expected << "\n";
                    session.has_matrix = false;
                    session.ready = false;
                    continue;
                }

                session.matrix = std::move(data);
                session.has_matrix = true;
                session.ready = false;
            } else if (cmd == proto::CMD_START) {
                if (!session.has_matrix || session.n == 0) {
                    std::cerr << "[!] START without valid matrix\n";
                    session.ready = false;
                    if (!proto::send_u32(client_sock, proto::CMD_START) ||
                        !proto::send_u32(client_sock, 0U)) {
                        break;
                    }
                    continue;
                }

                session.result = compute_variant_10(session.matrix, session.n, session.num_threads);
                session.ready = true;

                if (!proto::send_u32(client_sock, proto::CMD_START) ||
                    !proto::send_u32(client_sock, 1U)) {
                    break;
                }
            } else if (cmd == proto::CMD_STATUS) {
                if (!proto::send_u32(client_sock, proto::CMD_STATUS)) {
                    break;
                }
                if (!proto::send_u32(client_sock, session.ready ? 1U : 0U)) {
                    break;
                }
            } else if (cmd == proto::CMD_GET_RESULT) {
                if (!proto::send_u32(client_sock, proto::CMD_GET_RESULT)) {
                    break;
                }

                if (session.ready) {
                    if (!proto::send_i32_vector(client_sock, session.result)) {
                        break;
                    }
                } else {
                    // Якщо результат ще не готовий, повертаємо порожній масив
                    if (!proto::send_u32(client_sock, 0)) {
                        break;
                    }
                }
            } else {
                std::cerr << "[!] Unknown command: " << cmd << "\n";
                break;
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[!] Client handler exception: " << ex.what() << "\n";
    } catch (...) {
        std::cerr << "[!] Unknown client handler exception\n";
    }

    proto::close_socket(client_sock);
    std::cout << "[-] Client disconnected: " << ip_str << ":" << ntohs(client_addr.sin_port) << "\n";
}

}  // namespace

int main() {
    try {
        WinSockContext winsock;

        SOCKET server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_sock == INVALID_SOCKET) {
            std::cerr << "socket() failed: " << WSAGetLastError() << "\n";
            return 1;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(proto::DEFAULT_PORT);
        server_addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
            std::cerr << "bind() failed: " << WSAGetLastError() << "\n";
            proto::close_socket(server_sock);
            return 1;
        }

        if (listen(server_sock, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "listen() failed: " << WSAGetLastError() << "\n";
            proto::close_socket(server_sock);
            return 1;
        }

        std::cout << "Server started on port " << proto::DEFAULT_PORT << "\n";

        while (true) {
            sockaddr_in client_addr{};
            int client_len = sizeof(client_addr);
            SOCKET client_sock = accept(server_sock, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_sock == INVALID_SOCKET) {
                std::cerr << "accept() failed: " << WSAGetLastError() << "\n";
                continue;
            }

            std::thread client_thread(handle_client, client_sock, client_addr);
            client_thread.detach();
        }

        proto::close_socket(server_sock);
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Fatal unknown error\n";
        return 1;
    }

    return 0;
}
