#include "text_util.h"

namespace tu {

int compareNoCase(const wchar_t* a, const wchar_t* b) {
    if (!a || !b) return (a ? 1 : 0) - (b ? 1 : 0);
    while (*a && *b) {
        wchar_t ca = *a;
        wchar_t cb = *b;
        if (ca >= L'A' && ca <= L'Z') ca = (wchar_t)(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = (wchar_t)(cb - L'A' + L'a');
        if (ca != cb) return (int)ca - (int)cb;
        ++a;
        ++b;
    }
    return (int)*a - (int)*b;
}

int compare(const wchar_t* a, const wchar_t* b) {
    if (!a || !b) return (a ? 1 : 0) - (b ? 1 : 0);
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (int)*a - (int)*b;
}

bool contains(const wchar_t* haystack, const wchar_t* needle) {
    if (!haystack || !needle || !*needle) return false;
    for (const wchar_t* start = haystack; *start; ++start) {
        const wchar_t* h = start;        const wchar_t* n = needle;
        while (*h && *n && *h == *n) {
            ++h;
            ++n;
        }
        if (!*n) return true;
    }
    return false;
}

std::wstring::size_type lastPathSeparator(const std::wstring& path) {
    for (std::wstring::size_type i = path.size(); i > 0; --i) {
        const wchar_t c = path[i - 1];
        if (c == L'\\' || c == L'/') return i - 1;
    }
    return std::wstring::npos;
}

bool containsChar(const wchar_t* set, wchar_t value) {
    if (!set) return false;
    for (const wchar_t* cursor = set; *cursor; ++cursor) {
        if (*cursor == value) return true;
    }
    return false;
}

void copy(wchar_t* destination, std::size_t capacity, const wchar_t* source) {
    if (!destination || capacity == 0) return;
    std::size_t i = 0;
    if (source) {
        for (; i + 1 < capacity && source[i]; ++i) destination[i] = source[i];
    }
    destination[i] = 0;
}

int toInt(const wchar_t* text) {
    if (!text) return 0;
    int sign = 1;
    if (*text == L'-') {
        sign = -1;
        ++text;
    } else if (*text == L'+') {
        ++text;
    }
    int value = 0;
    while (*text >= L'0' && *text <= L'9') {
        value = value * 10 + (int)(*text - L'0');
        ++text;
    }
    return sign * value;
}

wchar_t* formatUnsigned(wchar_t* buffer, std::size_t capacity, unsigned long value) {
    if (!buffer || capacity == 0) return buffer;
    wchar_t digits[24];
    int count = 0;
    do {
        digits[count++] = (wchar_t)(L'0' + value % 10);
        value /= 10;
    } while (value && count < (int)(sizeof(digits) / sizeof(digits[0])));

    std::size_t i = 0;
    while (count > 0 && i + 1 < capacity) buffer[i++] = digits[--count];
    buffer[i] = 0;
    return buffer;
}

}  // namespace tu
