// ccskin.h - the layout of Access' Virus Control Center (its editor.xml, loaded at runtime from the user's own
// installer by AccessFiles::open_control_center; nothing of it is in this repository), as a tree for TI Revive's
// Control Center app.
//
// The format (read from the file, 2026-10-15): <editor><frame width height> with nested <container> (left top width
// height, forecolor/backcolor as RRGGBBAA hex, optional background PNG and a visibility `expression`) and <control>
// (type radio/button/textlabel/animbutton/animation/menu/patchbrowser, position, `parameter`, `value`, `expression`,
// `bitmap`, `background`). Positions are relative to the parent container; here they are made absolute. Bitmap
// names drop the "PNG/" prefix. What the parameters and expressions mean is decided by the app, not by the original's
// engine.xml.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vti::cc {

struct SkinNode {
    bool container = false;
    std::string type; // control type ("" for a container)
    int x = 0, y = 0, w = 0, h = 0; // absolute, in frame pixels
    uint32_t fore = 0xffffffffu, back = 0; // RRGGBBAA
    std::string parameter, expression, bitmap, background;
    int value = 0;
    std::vector<SkinNode> children;
};

struct SkinLayout {
    int width = 0, height = 0;
    std::vector<SkinNode> nodes; // the frame's children
};

bool parse_skin(const std::vector<uint8_t> &editor_xml, SkinLayout &out, std::string &error);

// false for the original's elements TI Revive leaves out entirely: the Burn To Flash ROM and OS Updater tabs (they
// write the Virus' flash, AGENTS.md rule 2; not drawn since 2026-11-19, before only not clickable)
inline bool skin_node_offered(const SkinNode &n)
{
    return !(n.parameter == "ControlCenter/Tabs/ActiveTab" && (n.value == 1 || n.value == 2));
}

// "PNG/BG_Header.png" -> "BG_Header.png"
std::string skin_bitmap_name(const std::string &ref);

} // namespace vti::cc
