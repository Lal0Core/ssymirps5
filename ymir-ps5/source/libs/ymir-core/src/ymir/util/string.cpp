#include <ymir/util/string.hpp>

#include <array>
#include <type_traits>

// For string <-> wstring conversions
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <stringapiset.h>
#elif defined(__ANDROID__) && __ANDROID_API__ < 28
    // Bionic only provides iconv from API 28 onwards
#elif defined(PS5)
    // The PS5's libc has no iconv; wchar_t is UTF-32 there, converted by hand below.
#else
    #include <cassert>
    #include <iconv.h>
#endif

namespace util {

struct ReplacementChar {
    const char *normal = nullptr;
    const char *dakuten = nullptr;
    const char *handakuten = nullptr;
};

std::string TranslateSaturnString(std::string_view str) {
    static constexpr std::array<ReplacementChar, 256> kTable = {{
#include "jp_char_table.inc"
    }};

    std::string output;
    output.reserve(str.size());

    for (size_t i = 0; i < str.size(); ++i) {
        const auto ch = static_cast<unsigned char>(str[i]);
        const ReplacementChar &entry = kTable[ch];

        // Look ahead for dakuten or handakuten
        if (i + 1 < str.size()) {
            const auto next = static_cast<unsigned char>(str[i + 1]);

            if (next == 0xDE) {
                // Dakuten suffix
                if (entry.dakuten) {
                    output += entry.dakuten;
                    ++i;
                    continue;
                } else {
                    output += entry.normal;
                    // output += "゛";
                    output += "¨"; // font doesn't have the standalone symbol
                    ++i;
                    continue;
                }
            } else if (next == 0xDF) {
                // Handakuten suffix
                if (entry.handakuten) {
                    output += entry.handakuten;
                    ++i;
                    continue;
                } else {
                    output += entry.normal;
                    // output += "゜";
                    output += "°"; // font doesn't have the standalone symbol
                    ++i;
                    continue;
                }
            }
        }

        // No suffix
        output += entry.normal;
    }

    return output;
}

std::string TrimWhitespace(std::string str) {
    auto start = str.find_first_not_of(" ");
    auto end = str.find_last_not_of(" ");

    if (start == std::string::npos && end == std::string::npos) {
        // The entire string is whitespace
        return "";
    }
    if (start == std::string::npos) {
        start = 0;
    }
    if (end == std::string::npos) {
        end = str.size();
    }
    return str.substr(start, end + 1);
}

std::wstring StringToWString(std::string_view str) {
    if (str.empty()) {
        return L"";
    }

#ifdef _WIN32
    // Windows implementation
    const int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), nullptr, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size);
    return wstr;

#elif defined(__ANDROID__) && __ANDROID_API__ < 28
    // TODO: implement without iconv
    return L"";

#elif defined(PS5)
    // UTF-8 -> wchar_t (UTF-16 on the PS5 target, UTF-32 elsewhere). Malformed input yields an empty string, like
    // the iconv path.
    std::wstring wstr;
    wstr.reserve(str.size());
    for (size_t i = 0; i < str.size();) {
        const unsigned char lead = static_cast<unsigned char>(str[i]);
        size_t len;
        char32_t cp;
        if (lead < 0x80) {
            len = 1, cp = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            len = 2, cp = lead & 0x1F;
        } else if ((lead & 0xF0) == 0xE0) {
            len = 3, cp = lead & 0x0F;
        } else if ((lead & 0xF8) == 0xF0) {
            len = 4, cp = lead & 0x07;
        } else {
            return L"";
        }
        if (i + len > str.size()) {
            return L"";
        }
        for (size_t j = 1; j < len; ++j) {
            const unsigned char cont = static_cast<unsigned char>(str[i + j]);
            if ((cont & 0xC0) != 0x80) {
                return L"";
            }
            cp = (cp << 6) | (cont & 0x3F);
        }
        static constexpr char32_t kMin[] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < kMin[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return L"";
        }
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0x10000) {
                cp -= 0x10000;
                wstr.push_back(static_cast<wchar_t>(0xD800 | (cp >> 10)));
                wstr.push_back(static_cast<wchar_t>(0xDC00 | (cp & 0x3FF)));
            } else {
                wstr.push_back(static_cast<wchar_t>(cp));
            }
        } else {
            wstr.push_back(static_cast<wchar_t>(cp));
        }
        i += len;
    }
    return wstr;

#else
    // Linux/macOS/FreeBSD implementation

    // Open converted
    iconv_t cd = iconv_open("WCHAR_T", "UTF-8");
    assert(cd != (iconv_t)-1);

    // Gather parameters
    char *inBuf = const_cast<char *>(str.data());
    size_t inRemaining = str.size();

    size_t outLength = str.size();
    size_t outRemaining = outLength * sizeof(wchar_t);

    std::wstring wstr(outLength, L'\0');
    char *outBuf = reinterpret_cast<char *>(wstr.data());

    // Convert string
    size_t result = iconv(cd, &inBuf, &inRemaining, &outBuf, &outRemaining);
    if (result == (size_t)-1) {
        iconv_close(cd);
        // TODO: handle error
        // throw std::runtime_error("iconv conversion failed due to an invalid character sequence.");
        return L"";
    }
    iconv_close(cd);

    size_t bytesWritten = outLength * sizeof(wchar_t) - outRemaining;
    size_t charsWritten = bytesWritten / sizeof(wchar_t);
    wstr.resize(charsWritten);

    return wstr;

#endif
}

std::string WStringToString(std::wstring_view wstr) {
    if (wstr.empty()) {
        return "";
    }

#ifdef _WIN32
    // Windows implementation
    const int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size, nullptr, nullptr);
    return str;

#elif defined(__ANDROID__) && __ANDROID_API__ < 28
    // TODO: implement without iconv
    return "";

#elif defined(PS5)
    // wchar_t (UTF-16 on the PS5 target, UTF-32 elsewhere) -> UTF-8. Invalid input yields an empty string, like
    // the iconv path.
    std::string str;
    str.reserve(wstr.size());
    for (size_t i = 0; i < wstr.size(); ++i) {
        char32_t cp = static_cast<char32_t>(static_cast<std::make_unsigned_t<wchar_t>>(wstr[i]));
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < wstr.size()) {
                const char32_t lo = static_cast<char16_t>(wstr[i + 1]);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    ++i;
                }
            }
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return "";
        }
        if (cp < 0x80) {
            str.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            str.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            str.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            str.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            str.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            str.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            str.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            str.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            str.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            str.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return str;

#else
    // Linux/macOS/FreeBSD implementation

    // Open converted
    iconv_t cd = iconv_open("UTF-8", "WCHAR_T");
    assert(cd != (iconv_t)-1);

    // Gather parameters
    char *inBuf = reinterpret_cast<char *>(const_cast<wchar_t *>(wstr.data()));
    size_t inRemaining = wstr.size() * sizeof(wchar_t);

    // Worst case for UTF-8 is 4 bytes per character
    size_t outLength = wstr.size() * 4;
    size_t outRemaining = outLength;

    std::string str(outLength, '\0');
    char *outBuf = str.data();

    // Convert string
    size_t result = iconv(cd, &inBuf, &inRemaining, &outBuf, &outRemaining);
    if (result == (size_t)-1) {
        iconv_close(cd);
        // TODO: handle error
        // throw std::runtime_error("iconv conversion failed due to an invalid character sequence.");
        return "";
    }
    iconv_close(cd);

    size_t bytesWritten = outLength - outRemaining;
    size_t charsWritten = bytesWritten;
    str.resize(charsWritten);

    return str;

#endif
}

} // namespace util
