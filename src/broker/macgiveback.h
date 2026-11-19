// macgiveback.h - how the macOS broker that captured the Virus (libusb audio, E511) gives it back to Apple's driver.
// libusb's attach re-enumerates the device and waits up to 10 s for it to come back; on #37's Catalina Mac that
// failed after 10.15 s, and libusb_close / libusb_exit then aborted ("Assertion failed: (refcnt >= 0)" in
// libusb_unref_device; reading of libusb 1.0.30's darwin_usb.c: the failed re-enumeration leaves the device counts
// broken). So after a failed attach the broker skips both and leaves: the process ends anyway, and a replug gives the
// Virus to Apple's driver. Header-only so the order is tested without hardware.
#pragma once

typedef struct {
    int (*attach)(void *ctx); // libusb_attach_kernel_driver: 0 = Apple's driver has the Virus again
    void (*close)(void *ctx); // libusb_close
    void (*exit)(void *ctx);  // libusb_exit
} vti_macgiveback_ops;

// returns the attach result (0 when nothing was captured)
static inline int vti_macgiveback(const vti_macgiveback_ops *ops, void *ctx, int captured)
{
    int r = captured ? ops->attach(ctx) : 0;
    if (r)
        return r;
    ops->close(ctx);
    ops->exit(ctx);
    return 0;
}
