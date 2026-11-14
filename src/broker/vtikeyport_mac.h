// vtikeyport_mac.h - macOS: the Virus as MIDI sources for the DAW, as the Access driver had them (keyport.h): "Virus TI
// Synth" for the Virus keys (cable 1, #29) and "Virus TI MIDI" for what comes in at its DIN MIDI In (cable 0, E517,
// E521). CoreMIDI virtual sources, no driver, there while the broker runs. Virus to DAW only: no destination, so this
// opens no way for another program to send anything to the Virus (rule 2).
#pragma once
#include <stdint.h>

void vti_keyport_start(void);                     // in the background; a failure is only logged
void vti_keyport_send(const uint8_t *msg, int n); // one complete channel message, from the USB reader thread
void vti_dinport_send(const uint8_t *msg, int n); // the bytes of one cable-0 packet (a SysEx piece too), same thread
void vti_keyport_log_counts(void);                // what went to macOS, for the log when the broker exits
