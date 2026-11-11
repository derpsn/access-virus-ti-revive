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
