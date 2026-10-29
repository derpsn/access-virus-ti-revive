// factoryorder.h - the order of the plug-in classes in the VST3 factory (E374). Steinberg's AU wrapper takes the
// first effect class of the module it carries, so the Snow Audio Unit carries its own copy of the module with a
// marker file in Contents/Resources; a module that finds the marker in its own bundle lists the Snow class first.
// VST3 hosts never see that copy (it lies inside the component), so their order stays the same.
#pragma once
#include <array>
#include <string>

namespace vti {

// .../X.vst3/Contents/MacOS/<binary> -> .../X.vst3/Contents/Resources/snow-first; empty if the path has no two
// folders above the binary
inline std::string snow_first_marker(const std::string &binary)
{
    const size_t a = binary.rfind('/');
    if (a == std::string::npos || a == 0)
        return {};
    const size_t b = binary.rfind('/', a - 1); // .../Contents/MacOS
    if (b == std::string::npos || b == 0)
        return {};
    return binary.substr(0, b) + "/Resources/snow-first";
}

// 0 = TI, 1 = the original's id (its projects), 2 = Snow
inline std::array<int, 3> class_order(bool snow_first)
{
    return snow_first ? std::array<int, 3>{2, 0, 1} : std::array<int, 3>{0, 1, 2};
}

} // namespace vti
