// dualsched.h - Apple's dual scheduling mode for instrument Audio Units (AudioUnitProperties.h,
// kMusicDeviceProperty_DualSchedulingMode = 1013): a host may set the property to 1, and from then on the frame value of
// MusicDeviceMIDIEvent, StartNote, StopNote and AudioUnitSetParameter is a bit field: the offset in the low 24 bits and
// a bit that says "scheduled ahead from a track". Without that bit the event comes from something played right now.
// This is how an Audio Unit can tell a key being pressed from a sequenced note (E401). Header only, no Apple headers:
// the AU uses it, the tests call it on every platform.
#pragma once
#include <cstdint>

namespace vti {

constexpr uint32_t DUAL_SCHEDULING_PROPERTY = 1013;
constexpr uint32_t DUAL_OFFSET_MASK = 0x00FFFFFF;
constexpr uint32_t DUAL_IS_SCHEDULED = 0x01000000;

struct DualSchedule {
    uint32_t offset; // what the event's sample offset really is
    bool live;       // played right now, as far as the host says so
};

// mode_on: the host has set the property. Without it the value is passed on as it came: nothing says it is a bit field.
inline DualSchedule dual_schedule(bool mode_on, uint32_t frame)
{
    if (!mode_on)
        return {frame, false};
    return {frame & DUAL_OFFSET_MASK, (frame & DUAL_IS_SCHEDULED) == 0};
}

} // namespace vti
