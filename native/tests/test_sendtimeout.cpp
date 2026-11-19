// sendtimeout.h (audit 2026-11-17 #1): a client that stops reading must not block the broker's send forever; the
// broker holds the client-list lock while it sends, so one stuck client stalled the MIDI reader and every other client
extern "C" {
#include "sendtimeout.h"
}
#include "check.h"
#include <chrono>
#include <future>
#include <sys/socket.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 // macOS: no such flag
#endif

TEST(sendtimeout_send_to_a_client_that_stops_reading_gives_up)
{
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    CHECK(vti_send_timeout(sv[0], 100) == 0);
    // sv[1] never reads: fill its buffer; a send must fail within the timeout instead of blocking
    auto r = std::async(std::launch::async, [&] {
        static char block[4096];
        for (int i = 0; i < 100000; i++)
            if (send(sv[0], block, sizeof block, MSG_NOSIGNAL) < 0)
                return true;
        return false;
    });
    bool returned = r.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    if (!returned)
        shutdown(sv[1], SHUT_RDWR); // unblock the sender so the test ends
    CHECK(returned);
    CHECK(r.get());
    close(sv[0]);
    close(sv[1]);
}

TEST(sendtimeout_zero_means_blocking_again)
{
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    CHECK(vti_send_timeout(sv[0], 100) == 0);
    CHECK(vti_send_timeout(sv[0], 0) == 0); // the observer path: its own writer thread may wait
    struct timeval tv = {1, 1};
    socklen_t n = sizeof tv;
    CHECK(getsockopt(sv[0], SOL_SOCKET, SO_SNDTIMEO, &tv, &n) == 0);
    CHECK(tv.tv_sec == 0 && tv.tv_usec == 0);
    close(sv[0]);
    close(sv[1]);
}
