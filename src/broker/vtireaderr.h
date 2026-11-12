// vtireaderr.h - when the Windows MIDI reader (vticore_winusb.c) gives up on the device.
//
// Why: a reader that keeps going on a removed device leaves the broker on a stale handle, still "present" for the
// plug-in, and it never opens the Virus again after it re-enumerates. ERROR_NO_SUCH_DEVICE (433) was missing from the
// list (sim S124; #36's log has it at a 12 s drop-off). Plain numbers so the rule can be tested off Windows.
#pragma once

// e = GetLastError() of a failed WinUsb_ReadPipe (not a timeout); fails = error-31 reads in a row, this one included
static inline int vti_read_error_dead(unsigned long e, int fails)
{
    return e == 22 /* ERROR_BAD_COMMAND */ || e == 1167 /* ERROR_DEVICE_NOT_CONNECTED */ ||
           e == 6 /* ERROR_INVALID_HANDLE */ || e == 433 /* ERROR_NO_SUCH_DEVICE */ ||
           (e == 31 /* ERROR_GEN_FAILURE */ && fails > 50); // removed, or stalled beyond recovery
}

// The libusb reader (vticore.c, macOS and Linux) by the same rule. Why: it retried every error but NO_DEVICE at once,
// so a stalled endpoint spun at ~25000 reads a second (#37: 6.3 M LIBUSB_ERROR_PIPE lines in 2 minutes on Catalina,
// until the unit was gone). Plain libusb numbers so the rule can be tested without libusb.
// r = the libusb error of a failed read (not a timeout); fails = errors in a row, this one included
static inline int vti_libusb_read_dead(int r, int fails)
{
    return r == -4 /* LIBUSB_ERROR_NO_DEVICE */ || fails > 50; // gone, or broken beyond recovery: close and reopen
}

// a stall (LIBUSB_ERROR_PIPE): clear the endpoint's halt, as WinUsb_ResetPipe does on Windows
static inline int vti_libusb_stalled(int r) { return r == -9; }

// only the first errors of a run go to the log; the reader writes one summary line when the run ends
static inline int vti_read_error_logged(int fails) { return fails <= 3; }
