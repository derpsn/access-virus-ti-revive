// giveback.h - E312: how the Linux broker hands the Virus back to snd-usb-audio. All five interfaces are claimed (the
// card is freed, E113), the port is reset while the broker still holds them, and only then they are released, so
// snd-usb-audio probes the Virus once after the reset. Released first, snd-usb-audio started probing at once and the
// reset hit it mid-probe; once in five give-backs the next probe failed with -71 and the Virus hung until a power
// cycle (E136, E300, E312). Header-only so the order is tested without hardware.
#pragma once

typedef struct {
    int (*claim)(void *ctx, int iface);
    int (*release)(void *ctx, int iface);
    int (*reset)(void *ctx);
} vti_giveback_ops;

// returns 1 if the port was reset; `reset_ok` = the reset guard's answer (E136)
static inline int vti_giveback(const vti_giveback_ops *ops, void *ctx, int reset_ok)
{
    for (int i = 0; i <= 4; i++)
        ops->claim(ctx, i);
    if (reset_ok)
        ops->reset(ctx);
    for (int i = 0; i <= 4; i++)
        ops->release(ctx, i);
    return reset_ok ? 1 : 0;
}
