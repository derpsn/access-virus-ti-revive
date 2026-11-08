// manuals.h - the Access manuals for the "?" menu, as the original's "?" lists them. TI Revive ships no Access file:
// its setup (install.sh, tirevive-access.ps1, "Set up Access Virus TI files.command") copies the Documentation folder
// from the user's own Access installer next to the Access files, so the PDFs are plain files in <...>/Virus TI/
// Documentation/Manuals (macOS: /Library/Documentation/Access Music/Virus TI/Manuals; a plain Windows Access install:
// Program Files (x86)). Names as in the Access installer 5.1.7 (MSI File table, macOS Additional_items payload).
// Header only; finding a file takes an exists() function so it can be tested without a disk.
#pragma once
#include <string>
#include <vector>

namespace vti {

enum : int { kManualReference, kManualQuickstart, kManualAtomizer, kManualTutorial, kManualProgramming, kManualCount };

// the file name; the Snow has its own Reference and Quickstart
inline std::string manual_file(int m, bool snow)
{
    switch (m) {
    case kManualReference: return snow ? "Virus TI Snow Reference.pdf" : "Virus TI Reference.pdf";
    case kManualQuickstart: return snow ? "Virus TI Snow Quickstart.pdf" : "Virus TI Quickstart.pdf";
    case kManualAtomizer: return "Atomizer Manual.pdf";
    case kManualTutorial: return "Virus Control Tutorial.pdf";
    case kManualProgramming: return "Programming Analogue Synths.pdf";
    }
    return {};
}

// the menu text
inline const char *manual_title(int m, bool snow)
{
    switch (m) {
    case kManualReference: return snow ? "Virus TI Snow Reference Manual (PDF)" : "Virus TI Reference Manual (PDF)";
    case kManualQuickstart: return snow ? "Virus TI Snow Quickstart (PDF)" : "Virus TI Quickstart (PDF)";
    case kManualAtomizer: return "Atomizer Manual (PDF)";
    case kManualTutorial: return "Virus Control Tutorial (PDF)";
    case kManualProgramming: return "Programming Analogue Synths (PDF)";
    }
    return "";
}

// Documentation roots to look in, best first. access_source = AccessFiles::source(): the Virus Control.dll in <...>/
// Virus TI/Common (Windows, Linux) or the bundle's Contents/Resources (macOS). program_files_x86 = %ProgramFiles(x86)%
// or "".
inline std::vector<std::string> manual_dirs(const std::string &access_source, const std::string &program_files_x86)
{
    std::vector<std::string> d;
    std::string src = access_source;
    for (char &c : src)
        if (c == '\\') // Windows paths may come with either separator
            c = '/';
    size_t at = src.rfind("/Common/");
    if (at != std::string::npos) // <...>/Virus TI
        d.push_back(src.substr(0, at) + "/Documentation");
    if (!program_files_x86.empty())
        d.push_back(program_files_x86 + "/Access Music/Virus TI/Documentation");
    d.push_back("/Library/Documentation/Access Music/Virus TI");
    return d;
}

// where manual m sits below a Documentation root: the MSI keeps the programming tutorial under Tutorials, the Mac
// package under Manuals
inline std::vector<std::string> manual_rel(int m, bool snow)
{
    const std::string f = manual_file(m, snow);
    if (m == kManualProgramming)
        return {"Tutorials/Programming Tutorial/" + f, "Manuals/" + f};
    return {"Manuals/" + f};
}

// the first existing file for manual m; a Snow takes the TI's Reference or Quickstart if its own is missing; "" if none
template <class Exists>
std::string find_manual(const std::vector<std::string> &dirs, int m, bool snow, Exists exists)
{
    for (int pass = 0; pass < (snow ? 2 : 1); pass++)
        for (auto &rel : manual_rel(m, snow && pass == 0))
            for (auto &dir : dirs)
                if (exists(dir + "/" + rel))
                    return dir + "/" + rel;
    return {};
}

} // namespace vti
