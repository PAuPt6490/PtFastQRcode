/* Memory and wide-string primitives, compiled as C so the MSVC intrinsics
 * (declared in <string.h>/<wchar.h>) do not conflict with these definitions. */
#include <stddef.h>
#include <intrin.h>

/* Tell the compiler these names are ordinary functions here, not intrinsics. */
#pragma function(memcpy, memset, memmove, memcmp, wcslen, wcscmp)

void* __cdecl memcpy(void* destination, const void* source, size_t count) {
    __movsb((unsigned char*)destination, (const unsigned char*)source, count);
    return destination;
}

void* __cdecl memset(void* destination, int value, size_t count) {
    __stosb((unsigned char*)destination, (unsigned char)value, count);
    return destination;
}

#pragma optimize("", off)
void* __cdecl memmove(void* destination, const void* source, size_t count) {
    unsigned char* d = (unsigned char*)destination;
    const unsigned char* s = (const unsigned char*)source;
    size_t i;
    if (d == s || count == 0) return destination;
    if (d < s || d >= s + count) {
        for (i = 0; i < count; ++i) d[i] = s[i];
    } else {
        for (i = count; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return destination;
}
#pragma optimize("", on)

int __cdecl memcmp(const void* left, const void* right, size_t count) {
    const unsigned char* a = (const unsigned char*)left;
    const unsigned char* b = (const unsigned char*)right;
    size_t i;
    for (i = 0; i < count; ++i) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

size_t __cdecl wcslen(const wchar_t* text) {
    const wchar_t* cursor = text;
    while (*cursor) ++cursor;
    return (size_t)(cursor - text);
}

int __cdecl wcscmp(const wchar_t* left, const wchar_t* right) {
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return (int)*left - (int)*right;
}

int __cdecl _wcsicmp(const wchar_t* left, const wchar_t* right) {
    while (*left && *right) {
        wchar_t a = *left;
        wchar_t b = *right;
        if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
        if (a != b) return (int)a - (int)b;
        ++left;
        ++right;
    }
    return (int)*left - (int)*right;
}

int __cdecl _wtoi(const wchar_t* text) {
    int sign = 1;
    int value = 0;
    if (!text) return 0;
    if (*text == L'-') {
        sign = -1;
        ++text;
    } else if (*text == L'+') {
        ++text;
    }
    while (*text >= L'0' && *text <= L'9') {
        value = value * 10 + (int)(*text - L'0');
        ++text;
    }
    return value * sign;
}

/* Marks that floating point is used; the CRT normally defines this. */
int _fltused = 1;

