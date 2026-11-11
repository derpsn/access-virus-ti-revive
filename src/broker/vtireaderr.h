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
