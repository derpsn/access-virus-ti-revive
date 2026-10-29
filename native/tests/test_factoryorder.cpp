// The Snow Audio Unit (device/factoryorder.h, E374): Steinberg's AU wrapper takes the first effect class of the VST3
// module it carries, so the Snow component carries its own copy of the module with a marker file, and a module that
// finds the marker in its own bundle lists the Snow class first. Hardware-free.
#include "check.h"
#include "../src/device/factoryorder.h"

TEST(factoryorder_marker_next_to_the_binary)
{
    CHECK(vti::snow_first_marker("/L/C/TI Revive Virus Control (Snow).component/Contents/Resources/plugin.vst3/"
                                 "Contents/MacOS/TI Revive Virus Control") ==
          "/L/C/TI Revive Virus Control (Snow).component/Contents/Resources/plugin.vst3/Contents/Resources/snow-first");
    CHECK(vti::snow_first_marker("no-folders").empty()); // not inside a bundle: no marker
    CHECK(vti::snow_first_marker("/a/b").empty());
}

TEST(factoryorder_snow_first_only_with_the_marker)
{
    // 0 = TI, 1 = the original's id (its projects), 2 = Snow
    auto plain = vti::class_order(false), snow = vti::class_order(true);
    CHECK(plain[0] == 0 && plain[1] == 1 && plain[2] == 2);
    CHECK(snow[0] == 2 && snow[1] == 0 && snow[2] == 1);
}
