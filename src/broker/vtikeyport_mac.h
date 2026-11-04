// vtikeyport_mac.h - macOS: the Virus keys as a MIDI source for the DAW, "Virus TI Synth" (keyport.h, #29). A CoreMIDI
// virtual source, no driver, there while the broker runs. Virus to DAW only: no destination, so this opens no way for
// another program to send anything to the Virus (rule 2).
#pragma once
#include <stdint.h>

void vti_keyport_start(void);                     // in the background; a failure is only logged
void vti_keyport_send(const uint8_t *msg, int n); // one complete channel message, from the USB reader thread
