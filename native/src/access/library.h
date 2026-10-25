// library.h - the original's patch libraries: Standard MIDI files with Virus single dumps in
// Documents/Access Music/Virus TI/Patches ("Personal Libraries"; Virus Control: "file://{mydocuments}/Access
// Music/Virus TI/Patches") and the same folder below the user's and the shared application data (E307).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vti {

struct PatchLibrary {
    std::string name, path;                   // file name without ".mid"
    std::vector<std::vector<uint8_t>> dumps; // 524-byte single dumps (TI format) by position; empty = a gap
    int skipped = 0; // single dumps left out: not 267/524 bytes (damaged), Virus C without page B, not convertible
};

// one .mid or .syx file (`lib.name` is left to the caller); false if it cannot be read. Each patch at its program
// byte like the original (E298), gaps empty; the file order when program bytes repeat
bool load_library_file(const std::string &path, PatchLibrary &lib, const std::vector<uint8_t> &page_b = {});
// `page_b`: AccessFiles::default_page_b() - with it, old Virus C (267-byte) dumps are upgraded to the TI format
std::vector<PatchLibrary> load_libraries(const std::string &dir, const std::vector<uint8_t> &page_b = {});
// writes `lib.dumps` to `lib.path` in the original's file layout (SMF type 0, one track, a SysEx per patch with
// delta 120, bank byte 1, program = position, division 96); gaps are left out
bool save_library(const PatchLibrary &lib, std::string &error);
// the original's "Create Empty Library" (E217): the name chosen in its save dialog ends in ".mid" (".midi" loses the
// "i", any other name gets ".mid"; case-sensitive like the original)
std::string library_file_name(const std::string &chosen);
// writes the original's empty library file to `path` (SMF type 0, one track with only its end, division 480),
// replacing an existing file
bool write_empty_library(const std::string &path, std::string &error);
// the folder for this platform. Linux: an existing ~/Documents/... (earlier installs), else the XDG documents folder
// (localized, e.g. ~/Dokumente), else the Wine prefix's Documents when that one exists, else ~/Documents/...
std::string library_dir();
// a place the original lists libraries from: its menu name and folder
struct LibraryRoot {
    std::string menu, dir;
};
// the original's roots (E307) in its order: "Personal Libraries" = `personal`, "User Libraries" and "Public
// Libraries" = `user` / `shared` + "/Access Music/Virus TI/Patches"; an empty base or a folder already listed is
// left out
std::vector<LibraryRoot> library_roots_for(const std::string &personal, const std::string &user,
                                           const std::string &shared);
// this platform's roots: Windows %APPDATA% / %PROGRAMDATA%, macOS ~/Library/Application Support and /Library/
// Application Support (where the Access installer puts the factory libraries); Linux only library_dir()
std::vector<LibraryRoot> library_roots();
// XDG_DOCUMENTS_DIR from the text of user-dirs.dirs ($HOME expanded); empty if missing, relative or the home itself
std::string xdg_documents_dir(const std::string &home, const std::string &user_dirs);

} // namespace vti
