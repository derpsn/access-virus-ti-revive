// Linux: what the broker's MIDI ports hand to the ALSA sequencer (seqevent.h) for the bytes keyport.h passes on, the
// Virus keys (port 1 "TI Revive Synth") and its DIN MIDI In (port 0 "TI Revive MIDI"). Hardware-free; Linux only (the
// kernel's sequencer header).
#include "check.h"
#ifdef __linux__
#include <cstring>
#include <vector>
extern "C" {
#include "seqevent.h"
}

namespace {
struct Out {
    snd_seq_event ev;
    std::vector<uint8_t> data; // a SysEx piece follows the event
};

Out enc(std::vector<uint8_t> msg, int port = 1)
{
    uint8_t buf[sizeof(snd_seq_event) + 64];
    Out o{};
    const int n = vti_seq_encode(msg.data(), (int)msg.size(), port, buf, (int)sizeof buf);
    if (n <= 0) {
        o.ev.type = 0xFF; // "nothing written"
        return o;
    }
    memcpy(&o.ev, buf, sizeof o.ev);
    o.data.assign(buf + sizeof o.ev, buf + n);
    return o;
}
} // namespace

TEST(seqevent_channel_messages_go_to_the_subscribers_at_once)
{
    Out o = enc({0x92, 60, 100});
    CHECK_EQ((int)o.ev.type, SNDRV_SEQ_EVENT_NOTEON);
    CHECK_EQ((int)o.ev.data.note.channel, 2);
    CHECK_EQ((int)o.ev.data.note.note, 60);
    CHECK_EQ((int)o.ev.data.note.velocity, 100);
    CHECK_EQ((int)o.ev.queue, SNDRV_SEQ_QUEUE_DIRECT);
    CHECK_EQ((int)o.ev.dest.client, SNDRV_SEQ_ADDRESS_SUBSCRIBERS);
    CHECK_EQ((int)o.ev.source.port, 1);
    CHECK_EQ((int)(o.ev.flags & SNDRV_SEQ_EVENT_LENGTH_MASK), SNDRV_SEQ_EVENT_LENGTH_FIXED);
    CHECK(o.data.empty());

    CHECK_EQ((int)enc({0x80, 60, 0}).ev.type, SNDRV_SEQ_EVENT_NOTEOFF);
    o = enc({0xA1, 61, 50});
    CHECK(o.ev.type == SNDRV_SEQ_EVENT_KEYPRESS && o.ev.data.note.note == 61 && o.ev.data.note.velocity == 50);
    o = enc({0xB3, 1, 64});
    CHECK(o.ev.type == SNDRV_SEQ_EVENT_CONTROLLER && o.ev.data.control.channel == 3);
    CHECK(o.ev.data.control.param == 1 && o.ev.data.control.value == 64);
    o = enc({0xC0, 5});
    CHECK(o.ev.type == SNDRV_SEQ_EVENT_PGMCHANGE && o.ev.data.control.value == 5);
    o = enc({0xD0, 77});
    CHECK(o.ev.type == SNDRV_SEQ_EVENT_CHANPRESS && o.ev.data.control.value == 77);
}

TEST(seqevent_pitch_bend_is_signed_around_the_centre)
{
    CHECK_EQ(enc({0xE0, 0, 64}).ev.data.control.value, 0);
    CHECK_EQ(enc({0xE0, 0, 0}).ev.data.control.value, -8192);
    CHECK_EQ(enc({0xE0, 127, 127}).ev.data.control.value, 8191);
    CHECK_EQ((int)enc({0xE5, 0, 64}).ev.data.control.channel, 5);
}

TEST(seqevent_system_messages_from_the_din_input)
{
    CHECK_EQ((int)enc({0xF8}, 0).ev.type, SNDRV_SEQ_EVENT_CLOCK);
    CHECK_EQ((int)enc({0xFA}, 0).ev.type, SNDRV_SEQ_EVENT_START);
    CHECK_EQ((int)enc({0xFB}, 0).ev.type, SNDRV_SEQ_EVENT_CONTINUE);
    CHECK_EQ((int)enc({0xFC}, 0).ev.type, SNDRV_SEQ_EVENT_STOP);
    CHECK_EQ((int)enc({0xFE}, 0).ev.type, SNDRV_SEQ_EVENT_SENSING);
    CHECK_EQ((int)enc({0xFF}, 0).ev.type, SNDRV_SEQ_EVENT_RESET);
    CHECK_EQ((int)enc({0xF6}, 0).ev.type, SNDRV_SEQ_EVENT_TUNE_REQUEST);
    Out o = enc({0xF2, 0x10, 0x02}, 0);
    CHECK(o.ev.type == SNDRV_SEQ_EVENT_SONGPOS && o.ev.data.control.value == 0x10 + (0x02 << 7));
    o = enc({0xF3, 7}, 0);
    CHECK(o.ev.type == SNDRV_SEQ_EVENT_SONGSEL && o.ev.data.control.value == 7);
    o = enc({0xF1, 0x35}, 0);
    CHECK(o.ev.type == SNDRV_SEQ_EVENT_QFRAME && o.ev.data.control.value == 0x35);
    CHECK_EQ((int)o.ev.source.port, 0);
}

// USB-MIDI carries SysEx in 3-byte pieces (vti_dinport_filter passes them as they come); each piece goes on as a
// variable-length SysEx event with its bytes right behind the event, as the kernel reads a write()
TEST(seqevent_sysex_pieces_are_variable_events_with_their_bytes)
{
    Out o = enc({0xF0, 0x00, 0x20}, 0);
    CHECK_EQ((int)o.ev.type, SNDRV_SEQ_EVENT_SYSEX);
    CHECK_EQ((int)(o.ev.flags & SNDRV_SEQ_EVENT_LENGTH_MASK), SNDRV_SEQ_EVENT_LENGTH_VARIABLE);
    CHECK_EQ((int)o.ev.data.ext.len, 3);
    CHECK(o.data == (std::vector<uint8_t>{0xF0, 0x00, 0x20}));
    o = enc({0x33, 0x01, 0x10}, 0); // a middle piece
    CHECK(o.ev.type == SNDRV_SEQ_EVENT_SYSEX && o.data.size() == 3);
    o = enc({0x7F, 0xF7}, 0); // the end
    CHECK(o.ev.type == SNDRV_SEQ_EVENT_SYSEX && o.data == (std::vector<uint8_t>{0x7F, 0xF7}));
    o = enc({0xF7}, 0);
    CHECK(o.ev.type == SNDRV_SEQ_EVENT_SYSEX && o.data.size() == 1);
}

TEST(seqevent_drops_what_it_cannot_write)
{
    CHECK_EQ((int)enc({}).ev.type, 0xFF);
    CHECK_EQ((int)enc({0x90, 60}).ev.type, 0xFF);   // a note-on without its velocity
    CHECK_EQ((int)enc({0xF4}).ev.type, 0xFF);       // undefined status
    uint8_t small[sizeof(snd_seq_event) - 1];
    const uint8_t on[] = {0x90, 60, 1};
    CHECK_EQ(vti_seq_encode(on, 3, 1, small, (int)sizeof small), 0); // no room
    uint8_t just[sizeof(snd_seq_event) + 2];
    const uint8_t sx[] = {0xF0, 0x00, 0x20};
    CHECK_EQ(vti_seq_encode(sx, 3, 0, just, (int)sizeof just), 0); // no room for the SysEx bytes
}
#endif
