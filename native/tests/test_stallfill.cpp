// The broker's USB audio on libusb fills silence for frames really lost (E078), and must not fill for slow clock drift
// (E439: at 22 ppm it added 30 ms that stayed, every 22 minutes) or for a thread held up while transfers stay queued.
#include "check.h"
extern "C" {
#include "stallfill.h"
}

// One completion every 10 ms of the Virus' clock, seen on a wall clock running `ppm` ahead of it.
static uint64_t run_steady(vti_stallfill &s, double &now, uint64_t &delivered, double seconds, double ppm)
{
    uint64_t filled = 0;
    const long n = (long)(seconds * 100);
    for (long i = 0; i < n; i++) {
        now += 10.0 * (1.0 + ppm * 1e-6);
        uint64_t g = vti_stallfill_step(&s, now, delivered);
        filled += g;
        delivered += g + 10;
    }
    return filled;
}

TEST(stallfill_slow_drift_never_fills)
{
    for (double ppm : {22.0, 31.0, -31.0, 100.0}) {
        vti_stallfill s = VTI_STALLFILL_INIT(10);
        double now = 1000;
        uint64_t delivered = 0;
        CHECK(run_steady(s, now, delivered, 4 * 3600, ppm) == 0); // four hours
    }
}

TEST(stallfill_held_thread_with_queued_transfers_fills_nothing)
{
    vti_stallfill s = VTI_STALLFILL_INIT(10);
    double now = 1000;
    uint64_t delivered = 0;
    run_steady(s, now, delivered, 60, 22);
    // the event thread is held up 70 ms: nothing was lost, the 7 waiting transfers then complete at once
    now += 70;
    for (int i = 0; i < 8; i++) {
        CHECK(vti_stallfill_step(&s, now, delivered) == 0);
        delivered += 10;
        now += 0.05;
    }
    CHECK(run_steady(s, now, delivered, 10, 22) == 0);
}

TEST(stallfill_real_loss_fills_what_was_lost)
{
    vti_stallfill s = VTI_STALLFILL_INIT(10);
    double now = 1000;
    uint64_t delivered = 0;
    run_steady(s, now, delivered, 60, 22);
    // 500 ms with nothing: 8 transfers of 10 ms were queued, so 420 ms were really lost
    now += 500;
    uint64_t filled = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t g = vti_stallfill_step(&s, now, delivered);
        filled += g;
        delivered += g + 10;
        now += 0.05;
    }
    filled += run_steady(s, now, delivered, 2, 22);
    CHECK(filled >= 410 && filled <= 430);
    CHECK(run_steady(s, now, delivered, 60, 22) == 0); // once, not again
}

TEST(stallfill_loss_after_long_drift_is_not_counted_with_the_drift)
{
    vti_stallfill s = VTI_STALLFILL_INIT(10);
    double now = 1000;
    uint64_t delivered = 0;
    run_steady(s, now, delivered, 3600, 22); // 79 ms of drift
    now += 200;
    uint64_t filled = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t g = vti_stallfill_step(&s, now, delivered);
        filled += g;
        delivered += g + 10;
        now += 0.05;
    }
    filled += run_steady(s, now, delivered, 2, 22);
    CHECK(filled >= 110 && filled <= 130); // 200 - 80 queued
}

TEST(stallfill_start_counts_the_first_transfer)
{
    vti_stallfill s = VTI_STALLFILL_INIT(10);
    CHECK(vti_stallfill_step(&s, 5000, 0) == 0);
}
