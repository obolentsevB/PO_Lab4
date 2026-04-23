#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

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

void print_matrix(const std::vector<int32_t>& matrix, uint32_t n, const std::string& title) {
    std::cout << title << " (" << n << "x" << n << "):\n";
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            std::cout << matrix[i * n + j] << "\t";
        }
        std::cout << "\n";
    }
}

std::vector<int32_t> generate_random_matrix(uint32_t n, int min_v = -10, int max_v = 20) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int32_t> dist(min_v, max_v);

    std::vector<int32_t> data;
    data.reserve(static_cast<size_t>(n) * n);

    for (uint32_t i = 0; i < n * n; ++i) {
        data.push_back(dist(gen));
    }

    return data;
}

SOCKET connect_to_server(const char* host, uint16_t port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        throw std::runtime_error("socket() failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        proto::close_socket(sock);
        throw std::runtime_error("inet_pton() failed");
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        proto::close_socket(sock);
        throw std::runtime_error("connect() failed");
    }

    return sock;
}

}

int main() {
    try {
        WinSockContext winsock;

        uint32_t n = 0;
        std::cout << "Enter matrix size N: ";
        std::cin >> n;
        if (!std::cin || n == 0 || n > 1024) {
            std::cerr << "Invalid value for N\n";
            return 1;
        }

        uint32_t num_threads = 0;
        std::cout << "Enter number of computation threads (1-64): ";
        std::cin >> num_threads;
        if (!std::cin || num_threads == 0 || num_threads > 64) {
            std::cerr << "Invalid number of threads (1-64)\n";
            return 1;
        }

        auto matrix = generate_random_matrix(n);
    print_matrix(matrix, n, "Generated matrix");

        SOCKET sock = connect_to_server(proto::DEFAULT_HOST, proto::DEFAULT_PORT);

        // Передаємо розмір матриці та кількість потоків
        if (!proto::send_u32(sock, proto::CMD_SET_SIZE) ||
            !proto::send_u32(sock, n) ||
            !proto::send_u32(sock, num_threads)) {
            throw std::runtime_error("SET_SIZE was not sent successfully");
        }

        // Передаємо дані матриці (row-major)
        if (!proto::send_u32(sock, proto::CMD_SEND_DATA) || !proto::send_i32_vector(sock, matrix)) {
            throw std::runtime_error("SEND_DATA was not sent successfully");
        }

        // Запускаємо обчислення та очікуємо підтвердження
        if (!proto::send_u32(sock, proto::CMD_START)) {
            throw std::runtime_error("START was not sent successfully");
        }

        {
            uint32_t start_resp_cmd = 0;
            uint32_t start_ok = 0;
            if (!proto::recv_u32(sock, start_resp_cmd) || !proto::recv_u32(sock, start_ok)) {
                throw std::runtime_error("Failed to receive response for START");
            }
            if (start_resp_cmd != proto::CMD_START || start_ok != 1U) {
                throw std::runtime_error("Server returned an error for START");
            }
        }

        // Перевіряємо готовність результату
        while (true) {
            if (!proto::send_u32(sock, proto::CMD_STATUS)) {
                throw std::runtime_error("STATUS was not sent successfully");
            }

            uint32_t resp_cmd = 0;
            uint32_t ready = 0;
            if (!proto::recv_u32(sock, resp_cmd) || !proto::recv_u32(sock, ready)) {
                throw std::runtime_error("Failed to receive STATUS response");
            }

            if (resp_cmd != proto::CMD_STATUS) {
                throw std::runtime_error("Invalid response for STATUS");
            }

            if (ready == 1U) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        // Отримуємо результат
        if (!proto::send_u32(sock, proto::CMD_GET_RESULT)) {
            throw std::runtime_error("GET_RESULT was not sent successfully");
        }

        uint32_t result_cmd = 0;
        if (!proto::recv_u32(sock, result_cmd)) {
            throw std::runtime_error("Failed to receive response for GET_RESULT");
        }

        if (result_cmd != proto::CMD_GET_RESULT) {
            throw std::runtime_error("Invalid response for GET_RESULT");
        }

        std::vector<int32_t> result;
        if (!proto::recv_i32_vector(sock, result)) {
            throw std::runtime_error("Failed to receive result array");
        }

        if (result.size() != static_cast<size_t>(n) * n) {
            throw std::runtime_error("Server returned an incorrect result size");
        }

        print_matrix(result, n, "Result after server processing");

        proto::close_socket(sock);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown error\n";
        return 1;
    }
}
