// The libusb reader's queue (src/broker/rxqueue.h): 8 reads stay pending on each MIDI endpoint, like the original's
// Mac driver (E521 (1)); the libusb callback only queues what came and resubmits, the reader thread hands it on.
// Why: with one blocking read whose thread also wrote to every client, a slow client left EP 0x85 unasked, and the
// Virus holds a key until the host asks (E517 (5)). Nothing may be lost: a full queue parks the transfer instead.
#include "check.h"
#include <cstring>
extern "C" {
#include "rxqueue.h"
}

namespace {
const uint8_t note[4] = {0x19, 0x90, 60, 100};
}

TEST(rxqueue_keeps_the_order_of_reads_and_errors)
{
    vti_rx_rec recs[8];
    vti_rxq q;
    vti_rxq_init(&q, recs, 8);
    CHECK_EQ(vti_rxq_push(&q, 2, 0, note, 4), 0);
    CHECK_EQ(vti_rxq_push(&q, 3, -9, nullptr, 0), 0); // a stall: no data
    CHECK_EQ(vti_rxq_push(&q, 4, 0, note, 2), 0);
    vti_rx_rec r;
    CHECK(vti_rxq_pop(&q, &r) == 0 && r.slot == 2 && r.err == 0 && r.len == 4 && !memcmp(r.data, note, 4));
    CHECK(vti_rxq_pop(&q, &r) == 0 && r.slot == 3 && r.err == -9 && r.len == 0);
    CHECK(vti_rxq_pop(&q, &r) == 0 && r.slot == 4 && r.len == 2);
    CHECK_EQ(vti_rxq_pop(&q, &r), -1); // empty
}

TEST(rxqueue_wraps_around)
{
    vti_rx_rec recs[3];
    vti_rxq q;
    vti_rxq_init(&q, recs, 3);
    vti_rx_rec r;
    for (int i = 0; i < 10; i++) {
        uint8_t b[1] = {(uint8_t)i};
        CHECK_EQ(vti_rxq_push(&q, 0, 0, b, 1), 0);
        CHECK(vti_rxq_pop(&q, &r) == 0 && r.data[0] == i);
    }
}

TEST(rxqueue_refuses_when_full_and_never_overwrites)
{
    vti_rx_rec recs[2];
    vti_rxq q;
    vti_rxq_init(&q, recs, 2);
    uint8_t a[1] = {1}, b[1] = {2}, c[1] = {3};
    CHECK_EQ(vti_rxq_push(&q, 0, 0, a, 1), 0);
    CHECK_EQ(vti_rxq_push(&q, 1, 0, b, 1), 0);
    CHECK_EQ(vti_rxq_push(&q, 2, 0, c, 1), -1);
    vti_rx_rec r;
    CHECK(vti_rxq_pop(&q, &r) == 0 && r.data[0] == 1);
}

TEST(rxqueue_cuts_a_record_to_its_buffer)
{
    vti_rx_rec recs[1];
    vti_rxq q;
    vti_rxq_init(&q, recs, 1);
    uint8_t big[100] = {0};
    CHECK_EQ(vti_rxq_push(&q, 0, 0, big, 100), 0);
    vti_rx_rec r;
    CHECK(vti_rxq_pop(&q, &r) == 0 && r.len == VTI_RX_MAX);
}

TEST(rxqueue_resubmits_only_with_room_for_every_pending_read)
{
    // 8 reads pending: each may complete before the reader takes anything, so a resubmit needs room for all of them
    vti_rx_rec recs[16];
    vti_rxq q;
    vti_rxq_init(&q, recs, 16);
    for (int i = 0; i < 8; i++)
        vti_rxq_push(&q, 0, 0, note, 4);
    CHECK(vti_rxq_may_resubmit(&q, 8));  // 8 queued + 8 pending = 16
    vti_rxq_push(&q, 0, 0, note, 4);
    CHECK(!vti_rxq_may_resubmit(&q, 8)); // park it
}

TEST(rxqueue_parked_transfers_come_back_in_slot_order)
{
    unsigned parked = 0;
    vti_park(&parked, 5);
    vti_park(&parked, 1);
    CHECK_EQ(vti_unpark(&parked), 1);
    CHECK_EQ(vti_unpark(&parked), 5);
    CHECK_EQ(vti_unpark(&parked), -1);
}

TEST(rxqueue_maps_transfer_status_to_libusb_errors)
{
    // the reader's rules (vtireaderr.h) speak libusb error codes; async transfers report a status
    CHECK_EQ(vti_xfer_err(0), 0);   // COMPLETED
    CHECK_EQ(vti_xfer_err(1), -1);  // ERROR -> LIBUSB_ERROR_IO
    CHECK_EQ(vti_xfer_err(2), -7);  // TIMED_OUT -> LIBUSB_ERROR_TIMEOUT
    CHECK_EQ(vti_xfer_err(4), -9);  // STALL -> LIBUSB_ERROR_PIPE
    CHECK_EQ(vti_xfer_err(5), -4);  // NO_DEVICE -> LIBUSB_ERROR_NO_DEVICE
    CHECK_EQ(vti_xfer_err(6), -8);  // OVERFLOW -> LIBUSB_ERROR_OVERFLOW
    CHECK_EQ(vti_xfer_err(3), -1);  // CANCELLED outside a close: as an IO error
    CHECK_EQ(vti_xfer_err(99), -1);
}
