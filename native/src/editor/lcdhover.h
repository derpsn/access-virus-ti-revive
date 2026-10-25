// lcdhover.h - the bottom LCD shows the control under the mouse, like the original (E353, E354): its name, value,
// the value stored in the loaded patch in brackets, and the part. Hover writes the four fields only when their text
// changed, and off a control clears them only if hover filled them (a message written meanwhile - "Virus connected",
// the bank sync - stays). Header only: the plug-in and the tests use it.
#pragma once
#include <string>

namespace vti {

struct LcdFields {
    std::string param, value, orig, part;
    bool operator==(const LcdFields &o) const
    {
        return param == o.param && value == o.value && orig == o.orig && part == o.part;
    }
};

// what the original leaves out: its editor's own parameters and the internal ones (browser state ...), except the
// MIDI standard parameters; and global settings (`part` "none": the LCD stays blank, E353)
inline bool lcd_hover_shows(const std::string &path, const std::string &part = std::string())
{
    auto starts = [&](const char *s) { return path.rfind(s, 0) == 0; };
    if (path.empty() || part == "none" || starts("VirusControl"))
        return false;
    return !starts("Internal") || starts("Internal MIDI Standard Parameters");
}

class LcdHover {
public:
    // the mouse over a shown control: true if the LCD changed
    bool enter(const LcdFields &f, LcdFields &lcd)
    {
        owned_ = true;
        if (f == lcd)
            return false;
        lcd = f;
        return true;
    }

    // off any shown control: clears what hover filled; true if the LCD changed
    bool leave(LcdFields &lcd)
    {
        if (!owned_)
            return false;
        owned_ = false;
        if (lcd == LcdFields{})
            return false;
        lcd = {};
        return true;
    }

private:
    bool owned_ = false;
};

} // namespace vti
