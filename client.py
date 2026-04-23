import random
import socket
import struct
import time

# Protocol constants
CMD_SET_SIZE = 1
CMD_SEND_DATA = 2
CMD_START = 3
CMD_STATUS = 4
CMD_GET_RESULT = 5

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 54000


def send_u32(sock: socket.socket, value: int) -> None:
    sock.sendall(struct.pack("!I", value))


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("Connection closed while reading")
        data += chunk
    return data


def recv_u32(sock: socket.socket) -> int:
    return struct.unpack("!I", recv_exact(sock, 4))[0]


def send_i32(sock: socket.socket, value: int) -> None:
    sock.sendall(struct.pack("!i", value))


def recv_i32(sock: socket.socket) -> int:
    return struct.unpack("!i", recv_exact(sock, 4))[0]


def send_i32_vector(sock: socket.socket, arr: list[int]) -> None:
    send_u32(sock, len(arr))
    for x in arr:
        send_i32(sock, x)


def recv_i32_vector(sock: socket.socket) -> list[int]:
    count = recv_u32(sock)
    return [recv_i32(sock) for _ in range(count)]


def print_matrix(mat: list[int], n: int, title: str) -> None:
    print(f"{title} ({n}x{n}):")
    for i in range(n):
        row = mat[i * n:(i + 1) * n]
        print("\t".join(str(v) for v in row))


def generate_random_matrix(n: int, min_v: int = -10, max_v: int = 20) -> list[int]:
    return [random.randint(min_v, max_v) for _ in range(n * n)]


def main() -> None:
    try:
        n = int(input("Enter matrix size N: ").strip())
        if n <= 0 or n > 1024:
            print("Invalid value for N")
            return

        num_threads = int(input("Enter number of threads for computation (1-64): ").strip())
        if num_threads <= 0 or num_threads > 64:
            print("Invalid number of threads (must be 1-64)")
            return

        matrix = generate_random_matrix(n)
        print_matrix(matrix, n, "Generated matrix")

        with socket.create_connection((DEFAULT_HOST, DEFAULT_PORT), timeout=10) as sock:
            # Задати розмір матриці та кількість потоків
            send_u32(sock, CMD_SET_SIZE)
            send_u32(sock, n)
            send_u32(sock, num_threads)

            # Передаємо дані матриці (row-major)
            send_u32(sock, CMD_SEND_DATA)
            send_i32_vector(sock, matrix)

            # Запускаємо обчислення та очікуємо підтвердження
            send_u32(sock, CMD_START)
            start_resp_cmd = recv_u32(sock)
            start_ok = recv_u32(sock)
            if start_resp_cmd != CMD_START or start_ok != 1:
                raise RuntimeError("Server returned error for START")

            # Перевіряємо готовність результату
            while True:
                send_u32(sock, CMD_STATUS)
                resp_cmd = recv_u32(sock)
                ready = recv_u32(sock)

                if resp_cmd != CMD_STATUS:
                    raise RuntimeError("Invalid response to STATUS")

                if ready == 1:
                    break

                time.sleep(0.2)

            # Отримуємо результат
            send_u32(sock, CMD_GET_RESULT)
            result_cmd = recv_u32(sock)
            if result_cmd != CMD_GET_RESULT:
                raise RuntimeError("Invalid response to GET_RESULT")

            result = recv_i32_vector(sock)
            if len(result) != n * n:
                raise RuntimeError("Server returned invalid result size")

            print_matrix(result, n, "Result after server processing")

    except ValueError:
        print("Input error: N must be an integer")
    except (OSError, ConnectionError, RuntimeError) as ex:
        print(f"Error: {ex}")


if __name__ == "__main__":
    main()
