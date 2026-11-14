// The original's two start-up pages (startprogress.h): "Starting up" with a filling ring while the project goes to
// the Virus (VirusControl/RecallProgress, ProgressBar.png: 16 frames), and "Syncing memory" only while banks are
// really fetched (InitialSyncProgress). 0..126 shows a page, 127 hides it (editordevice.xml conditions).
#include "check.h"
#include "device/startprogress.h"
#include <string>

using namespace vti;

TEST(startprogress_recall_follows_the_queue)
{
    RecallMeter m;
    CHECK(m.update(0) == 0);    // nothing queued yet: the page shows, empty
    CHECK(m.update(200) == 0);  // the state arrives: 200 messages to go
    CHECK(m.update(100) == 60); // half sent
    CHECK(m.update(250) == 0);  // a new state replaces the old one: the total grows, the ring starts again
    CHECK(m.update(0) == RecallMeter::SENT);
    CHECK(RecallMeter::SENT < 127); // the page stays until the Virus answers the ready probe
}

TEST(startprogress_sync_page_only_when_fetching)
{
    SyncPage s;
    CHECK(s.progress(0, false) == 127);   // the cache check: no page
    CHECK(s.progress(300, false) == 127);
    CHECK(s.progress(301, true) == 301 * 126 / 512); // the first real fetch shows it
    CHECK(s.shown());
    CHECK(s.progress(302, false) == 302 * 126 / 512); // and it stays until the sync is done
    CHECK(s.progress(512, false) == 126);
}

TEST(startprogress_sync_page_at_once_when_a_bank_is_missing)
{
    // a bank without a cache file is read in full: the page follows "Starting up" directly, without showing the
    // editor for the moment the bank check takes (user, L4)
    SyncPage s;
    s.expect();
    CHECK(s.shown());
    CHECK(s.progress(0, false) == 0);
    CHECK(s.progress(256, false) == 63);
}

// user ROM programs not cached yet (a bank burned in the Control Center, E459) are known before the RAM check:
// they will be read, so the page follows "Starting up" directly too, as the original's (user, 2026-11-08: the editor
// showed first and the page appeared only when ROM Z's reading began)
TEST(startprogress_sync_page_at_once_when_user_rom_is_missing)
{
    CHECK(sync_page_at_start(false, 0) == false); // current caches: the check alone shows nothing
    CHECK(sync_page_at_start(true, 0));           // a RAM bank without a cache file
    CHECK(sync_page_at_start(false, 117));        // user ROM programs to read
}

TEST(startprogress_sync_end_names_an_abort)
{
    // the sync stops at the first bank that fails (-1: a request without an answer) or is cancelled (-2: closed
    // while syncing); the log said "bank sync done" after such a stop (E289)
    CHECK(std::string(sync_end(0)) == "done");
    CHECK(std::string(sync_end(128)) == "done");
    CHECK(std::string(sync_end(-1)) == "failed");
    CHECK(std::string(sync_end(-2)) == "stopped");
}

// with user ROM banks to read (E381) the sync covers more than the 512 RAM programs: the page runs to the end
// of all of them, not to 100 % after RAM D
TEST(startprogress_sync_page_total)
{
    SyncPage s;
    s.total = 512 + 2 * 128;
    s.expect();
    CHECK(s.progress(512, false) == 512 * 126 / 768);
    CHECK(s.progress(768, true) == 126);
    s.total = 0; // never divides by zero
    CHECK(s.progress(10, true) == 126);
}

// the LCD while patches come from the Virus names the patch just fetched (user 2026-11-11: the Virus display shows
// which entry is synced, the plug-in should too); a patch only checked (current cache) or a blank name: no name
TEST(startprogress_sync_lcd_names_the_fetched_patch)
{
    CHECK(sync_lcd_title("Synchronizing RAM A", "PrismPadRD") == "Synchronizing RAM A: PrismPadRD");
    CHECK(sync_lcd_title("Downloading RAM B", "Pro1-FX XM") == "Downloading RAM B: Pro1-FX XM");
    CHECK(sync_lcd_title("Synchronizing RAM A", "") == "Synchronizing RAM A");
    CHECK(sync_lcd_title("Synchronizing RAM A", "   ") == "Synchronizing RAM A");
}

// the original hides the LCD texts while its sync page shows (skin condition "NOT Display Recall or Sync Progress"),
// so the patch just fetched goes on the page itself, under the progress bar, in the page's capitals
TEST(startprogress_sync_page_names_the_fetched_patch)
{
    CHECK(sync_page_item("RAM A", 85, "PrismPadRD") == "RAM A 85 / 128: PRISMPADRD");
    CHECK(sync_page_item("ROM Z", 1, "Pro1-FX XM") == "ROM Z 1 / 128: PRO1-FX XM");
    CHECK(sync_page_item("RAM B", 3, "  ") == "RAM B 3 / 128");
}

// the time left on the sync page counts only what is sure to be fetched (empty slots of the RAM banks still to come,
// plus user ROM programs), not 512: with only RAM A missing it said "about 9 min" for a 2-minute sync (2026-11-11)
TEST(startprogress_sync_eta_counts_only_what_is_missing)
{
    std::array<std::vector<bool>, 4> empty;
    for (auto &e : empty)
        e.assign(128, false);
    std::fill(empty[0].begin(), empty[0].end(), true); // RAM A without a cache file
    empty[2][5] = true;                                // one gap in RAM C
    CHECK_EQ(sync_remaining(empty, 0, 0, 0), 127 + 1);  // after RAM A #1: 127 of A, the gap in C
    CHECK_EQ(sync_remaining(empty, 0, 127, 0), 1);
    CHECK_EQ(sync_remaining(empty, 2, 5, 0), 0);
    CHECK_EQ(sync_remaining(empty, 1, 10, 128), 1 + 128); // user ROM programs come after RAM D
    CHECK_EQ(sync_eta_s(120, 0, 0), 124);                 // nothing measured yet: about 1.03 s each
    CHECK_EQ(sync_eta_s(60, 20000, 20), 60);              // measured: 1 s each
    CHECK_EQ(sync_eta_s(0, 20000, 20), 0);
}

// every letter goes to the page's capitals, the last of the alphabet too
TEST(startprogress_sync_page_capitals_include_z)
{
    CHECK(sync_page_item("RAM C", 7, "zaz") == "RAM C 7 / 128: ZAZ");
}

// the measured pace counts from the 8th fetch on, and only with time measured: fewer fetches or no worked time keep
// the 1.03 s estimate (a pace from one or two fetches, or 0 ms, would show a wrong or zero time left)
TEST(startprogress_sync_eta_needs_eight_measured_fetches)
{
    CHECK_EQ(sync_eta_s(10, 8 * 2000, 8), 20); // 8 measured: 2 s each
    CHECK_EQ(sync_eta_s(10, 7 * 2000, 7), 10); // 7: still the estimate
    CHECK_EQ(sync_eta_s(10, 2 * 3000, 2), 10);
    CHECK_EQ(sync_eta_s(10, 0, 8), 10); // no time measured: the estimate, not zero
}
