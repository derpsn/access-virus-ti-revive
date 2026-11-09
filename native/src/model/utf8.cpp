// utf8.cpp - see utf8.h. Windows converts by hand (MultiByteToWideChar/WideCharToMultiByte with CP_UTF8 and no
// error flags): the standard library's u8string() throws on a name that is not valid UTF-16, which NTFS allows.
#include "model/utf8.h"
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vti {

#ifdef _WIN32
std::wstring widen(const std::string &utf8)
{
    if (utf8.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring &wide)
{
    if (wide.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::filesystem::path u8path(const std::string &utf8) { return std::filesystem::path(widen(utf8)); }
std::string u8str(const std::filesystem::path &p) { return narrow(p.native()); }
std::string u8generic(const std::filesystem::path &p) { return narrow(p.generic_wstring()); }
FILE *u8fopen(const std::string &utf8, const char *mode) { return _wfopen(widen(utf8).c_str(), widen(mode).c_str()); }
std::string u8env(const char *name)
{
    const wchar_t *v = _wgetenv(widen(name).c_str());
    return v ? narrow(v) : std::string();
}
#else
std::filesystem::path u8path(const std::string &utf8) { return std::filesystem::path(utf8); }
std::string u8str(const std::filesystem::path &p) { return p.native(); }
std::string u8generic(const std::filesystem::path &p) { return p.generic_string(); }
FILE *u8fopen(const std::string &utf8, const char *mode) { return fopen(utf8.c_str(), mode); }
std::string u8env(const char *name)
{
    const char *v = getenv(name);
    return v ? std::string(v) : std::string();
}
#endif

namespace {
// U+00C0..U+017F (Latin-1 Supplement letters and Latin Extended-A) without their accents; '?' for the two signs
const char kLatin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"
                       "aaaaaaaceeeeiiiidnooooo?ouuuuyty";
const char kLatinA[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRr"
                       "SsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
static_assert(sizeof kLatin1 - 1 == 64 && sizeof kLatinA - 1 == 128, "one letter per code point");

bool combining(unsigned cp)
{
    return (cp >= 0x300 && cp <= 0x36F) || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) ||
           (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE20 && cp <= 0xFE2F);
}

// the code point at s[i] and its length; 0 = not a valid UTF-8 sequence (taken as one stray byte)
size_t decode(const std::string &s, size_t i, unsigned &cp)
{
    const unsigned char c = (unsigned char)s[i];
    const size_t n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (n == 0 || i + n > s.size())
        return 0;
    cp = n == 1 ? c : n == 2 ? c & 0x1F : n == 3 ? c & 0x0F : c & 0x07;
    for (size_t k = 1; k < n; k++) {
        const unsigned char d = (unsigned char)s[i + k];
        if ((d & 0xC0) != 0x80)
            return 0;
        cp = (cp << 6) | (d & 0x3F);
    }
    static const unsigned least[] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < least[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) // overlong or not a character
        return 0;
    return n;
}
} // namespace

std::string ascii_fold(const std::string &utf8)
{
    std::string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size();) {
        unsigned cp = 0;
        const size_t n = decode(utf8, i, cp);
        if (n == 0) {
            out += '?';
            i++;
            continue;
        }
        i += n;
        if (cp >= 32 && cp <= 127)
            out += (char)cp;
        else if (cp >= 0xC0 && cp <= 0xFF)
            out += kLatin1[cp - 0xC0];
        else if (cp >= 0x100 && cp <= 0x17F)
            out += kLatinA[cp - 0x100];
        else if (!combining(cp))
            out += '?';
    }
    return out;
}

} // namespace vti
