// Hardware-free test of concurrent Mac broker control requests.
// Uses the actual client implementation with an ordinary socket pair and protocol replies.
#include "../VirusClient.cpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <poll.h>
#include <thread>
#include <vector>

static bool receive_control(int fd, uint32_t &seq, uint16_t &index)
{
    uint16_t header[2];
    uint8_t body[4096];
    if (io_all(fd, header, sizeof header, 0) || header[0] != MSG_CONTROL ||
        header[1] < 12 || header[1] > sizeof body || io_all(fd, body, header[1], 0))
        return false;
    memcpy(&seq, body, sizeof seq);
    index = static_cast<uint16_t>(body[8] | body[9] << 8);
    return true;
}

int main()
{
    constexpr int count = 32;
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) return 2;
    g_fd = sockets[0];
    g_connected = 1;
    std::thread receiver(reader, nullptr);
    std::atomic<bool> server_ok{true}, start{false};
    std::thread server([&] {
        int received = 0;
        while (received < count) {
            std::vector<std::pair<uint32_t, uint16_t>> pending;
            uint32_t seq;
            uint16_t index;
            if (!receive_control(sockets[1], seq, index)) { server_ok = false; break; }
            pending.emplace_back(seq, index);
            ++received;
            // Replies may share one socket read after ordinary transport buffering.
            while (received < count) {
                pollfd p{sockets[1], POLLIN, 0};
                if (poll(&p, 1, 20) <= 0) break;
                if (!receive_control(sockets[1], seq, index)) { server_ok = false; break; }
                pending.emplace_back(seq, index);
                ++received;
            }
            for (const auto &request : pending) {
                uint16_t header[2] = {MSG_CONTROL_REPLY, 10};
                uint8_t reply[10];
                int32_t result = 2;
                memcpy(reply, &request.first, 4);
                memcpy(reply + 4, &result, 4);
                reply[8] = static_cast<uint8_t>(request.second);
                reply[9] = static_cast<uint8_t>(request.second >> 8);
                if (io_all(sockets[1], header, sizeof header, 1) ||
                    io_all(sockets[1], reply, sizeof reply, 1)) server_ok = false;
            }
        }
        shutdown(sockets[1], SHUT_WR);
        close(sockets[1]);
    });
    std::array<int, count> correct{};
    std::vector<std::thread> callers;
    for (int i = 0; i < count; ++i) {
        callers.emplace_back([&, i] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            uint8_t result[2] = {};
            const uint16_t expected = static_cast<uint16_t>(0x100 + i);
            int n = broker_control(0xc1, 0x81, 0, expected, result, sizeof result);
            correct[i] = n == 2 && result[0] == static_cast<uint8_t>(expected) &&
                         result[1] == static_cast<uint8_t>(expected >> 8);
        });
    }
    start.store(true, std::memory_order_release);
    for (auto &caller : callers) caller.join();
    server.join();
    receiver.join();
    int matches = 0;
    for (int ok : correct) matches += ok;
    printf("concurrent control replies: %d/%d correct\n", matches, count);
    return server_ok && matches == count ? 0 : 1;
}
