// startprogress.h - the values behind the original's two start-up pages.
//
// Why: the original shows "Starting up" with a ring that fills (VirusControl/RecallProgress, ProgressBar.png, 16
// frames) while the project goes to the Virus, and "Syncing memory with Virus" (InitialSyncProgress) while it reads
// the RAM banks; each page shows while its value is 0..126 and hides at 127 (conditions in editordevice.xml).
// TI Revive showed the syncing page for every start, also when the bank check found nothing to fetch (user,
// 2026-10-19: "only show the syncing if it really happens").
#pragma once

#include <string>

namespace vti {

// RecallProgress from the queued start-up messages and state; the largest count seen is the total (a project's
// state replaces the default one after it), so the ring never runs backwards except when a bigger state arrives
struct RecallMeter {
    static constexpr int SENT = 120; // all sent; the last part fills when the Virus answers the ready probe
    int total = 0;
    int update(int left)
    {
        if (left < 0)
            left = 0;
        if (left > total)
            total = left;
        return total == 0 ? 0 : (total - left) * SENT / total;
    }
};

// InitialSyncProgress: hidden (127) during the bank check, shown from the first patch that is really fetched
struct SyncPage {
    bool on = false;
    int total = 512; // programs the sync covers: RAM A-D, plus 128 per user ROM bank to read (E381)
    bool shown() const { return on; }
    void expect() { on = true; } // a bank without a cache file: it will be read, show the page from the start
    int progress(int done, bool fetched) // done = patches checked so far (of `total`)
    {
        if (fetched)
            on = true;
        if (!on)
            return 127;
        return done >= total ? 126 : done * 126 / total;
    }
};

// whether the page shows from the start, straight after "Starting up": only when the sync will certainly read - a RAM
// bank without a cache file, or user ROM programs not cached yet (known before the RAM check, e.g. a bank burned in
// the Control Center). A changed RAM bank shows only during the check: the page then comes with its first patch.
inline bool sync_page_at_start(bool ram_bank_missing, int rom_missing) { return ram_bank_missing || rom_missing > 0; }

// how the bank sync ended, for the log: it stops at the first bank that fails (-1, a request without an answer) or
// is cancelled (-2, closed while syncing); "bank sync done" after such a stop read like a full sync (E289)
inline const char *sync_end(int result) { return result == -1 ? "failed" : result == -2 ? "stopped" : "done"; }

// the LCD's first line while patches come from the Virus: the patch just fetched after the bank (user 2026-11-11: the
// Virus display shows which entry is synced, the plug-in shows it too); only checked or blank: the bank alone
inline std::string sync_lcd_title(const std::string &what, const std::string &name)
{
    return name.find_first_not_of(' ') == std::string::npos ? what : what + ": " + name;
}

} // namespace vti
