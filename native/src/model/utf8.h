// utf8.h - file paths as UTF-8 strings on every platform (#35).
// TI Revive keeps every path in a std::string holding UTF-8. On Windows, std::filesystem::path(std::string),
// path::string(), fopen() and getenv() use the ANSI code page instead: a name it cannot represent (Japanese on a
// Western system, a combining accent copied from a Mac, an emoji) made path::string() throw std::system_error, and
// load_banks() in initialize() crashed the host. These helpers convert between UTF-8 and the wide Windows APIs at the
// edge; on macOS and Linux paths are UTF-8 already and they pass the bytes through. None of them throws.
// Implemented in utf8.cpp (C++17, vticore): path::u8string() returns another type under C++20/23 (the AU target).
#pragma once
#include <cstdio>
#include <filesystem>
#include <string>

namespace vti {

// a path from UTF-8 (invalid UTF-8 becomes U+FFFD on Windows)
std::filesystem::path u8path(const std::string &utf8);
// the path as UTF-8, native separators (a name that is not valid UTF-16 on Windows gets U+FFFD instead of throwing)
std::string u8str(const std::filesystem::path &p);
// the path as UTF-8 with '/' separators
std::string u8generic(const std::filesystem::path &p);
// fopen() for a UTF-8 path (_wfopen on Windows)
FILE *u8fopen(const std::string &utf8, const char *mode);
// an environment variable as UTF-8 (_wgetenv on Windows); empty when it is not set
std::string u8env(const char *name);
// for the editor's bitmap font (ASCII 32..127): one character per code point, Latin letters without their accents,
// combining marks dropped, everything else '?' (also a byte that is not UTF-8)
std::string ascii_fold(const std::string &utf8);

#ifdef _WIN32
std::wstring widen(const std::string &utf8);
std::string narrow(const std::wstring &wide);
#endif

} // namespace vti
