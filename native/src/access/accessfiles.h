// accessfiles.h - the user's own Access "Virus TI" files (skin bitmaps, editor/device XML), read at runtime.
//
// Nothing of Access is shipped with this project: the skin and the device description come from the user's Access
// installation. Windows/Linux (Wine prefix): resources of type "XML" and "PNG" inside "Virus Control.dll";
// macOS: plain files in "Virus Control.bundle/Contents/Resources".
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vti {

// one entry of the REMOTE page's knob template list: its menu path ("/" = submenu) and MIDI string ("B0 05 FF")
struct KnobTemplate {
    std::string name, midi;
};

class AccessFiles {
public:
    // Locate and open the Access installation. `hint` may name Virus Control.dll / .bundle or its folder; empty =
    // search the standard locations (and $VTI_ACCESS_DIR). Returns nullptr and sets `error` if nothing is found.
    // Only the 64-bit (PE32+) Virus Control.dll counts; a 32-bit one is skipped and named in `error` (E345).
    static std::unique_ptr<AccessFiles> open(const std::string &hint, std::string &error);
    // The skin of Access' "Virus Control Center" (layout editor.xml + engine.xml and its PNGs, resources of
    // "Virus Control Center.exe"; a folder with those files also works), for TI Revive's Control Center app.
    // Searched next to the plug-in's Access files, in the Wine prefix and in Program Files; $VTI_ACCESS_CC overrides.
    static std::unique_ptr<AccessFiles> open_control_center(std::string &error);

    // File by its Access name, case-insensitive ("editor.xml", "BG_Page_Browser.png"); empty if missing.
    const std::vector<uint8_t> &get(const std::string &name) const;
    std::vector<std::string> names() const; // all names (lower case)
    const std::string &source() const { return source_; } // the DLL or bundle that was read
    // The init single patch embedded in the plug-in binary (a 524-byte single dump named " -Init-", stored with an
    // older format version; RemoteDevice::upgrade_single brings it up to date). Empty if not found.
    std::vector<uint8_t> init_single() const;
    // The fixed multi dump the original sends at start-up and stores in its state ("MultiCtrl!", 267 bytes).
    std::vector<uint8_t> init_multi() const;
    // the default page B the original gives old (Virus C, 267-byte) single dumps when upgrading them: 256 bytes
    // next to the embedded init patch in the plug-in binary (Windows DLL: 0x400 before it; Mac binary: 0x2a0 after
    // it, E346); empty if not plausible (all bytes must be 7-bit)
    std::vector<uint8_t> default_page_b() const;
    // the classic oscillator display's 64 waves (64 x 64 floats, the WaveSelect row first; E180), found in the plug-in
    // binary by content; empty if not found (the display then shows a sine for every wave)
    std::vector<float> osc_waves() const;
    // the REMOTE page's knob templates (find_knob_templates); empty if not found (the menu stays empty)
    std::vector<KnobTemplate> knob_templates() const;

private:
    std::vector<uint8_t> find_dump(uint8_t cmd, size_t size, size_t name_at, const char *name,
                                   std::vector<uint8_t> *binary = nullptr, size_t *offset = nullptr) const;
    std::map<std::string, std::vector<uint8_t>> files_; // key: lower-case name
    std::string source_;
    bool pe32_ = false; // load_pe read a 32-bit (PE32) image
    // `required`: a resource the file must contain to count (the plug-in's: remotedevice.xml)
    bool load_pe(const std::string &dll, std::string &error, const char *required = "remotedevice.xml");
    bool load_dir(const std::string &dir, std::string &error, const char *required = "remotedevice.xml");
};

// the 64 x 64 wave table inside a plug-in binary: row 0 is one sine period, every value within +-1; empty if absent
std::vector<float> find_osc_waves(const std::vector<uint8_t> &bin);

// the knob template list inside a plug-in binary (E197), found by shape: the first run of at least 8 (name, MIDI
// string) pointer pairs, 8-byte aligned; pointers resolved through a 64-bit PE's sections or a 64-bit Mach-O's
// segments (also inside a universal binary). Empty if not found.
std::vector<KnobTemplate> find_knob_templates(const std::vector<uint8_t> &bin);

} // namespace vti
