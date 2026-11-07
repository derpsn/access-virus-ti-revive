// closeseq.h - what the plug-in leaves the Virus with when it closes, as the original does (E391: 0x180070d20, on the wire
// in its captures): all three output cables back to 1, Mode SINGLE, 350 ms later Mode SINGLE again. Leaving MIDI REMOTE
// makes the Virus restore the settings it saved on entering it (E392 7). Without this a Virus stays in REMOTE after the
// plug-in, and at its next start restores them only after it forced Local on (E392 8, a prediction). Known writes only.
#pragma once
#include <cstdint>
#include <vector>

namespace vti {

constexpr int CLOSE_WAIT_MS = 350;

inline std::vector<uint8_t> close_global(uint8_t id, uint8_t value)
{
    return {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, id, value, 0xF7};
}

inline std::vector<std::vector<uint8_t>> close_messages_first()
{
    return {close_global(0x3B, 1), close_global(0x3A, 1), close_global(0x3C, 1), close_global(0x7A, 0)};
}

inline std::vector<uint8_t> close_message_last()
{
    return close_global(0x7A, 0);
}

} // namespace vti
