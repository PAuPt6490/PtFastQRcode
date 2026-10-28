// Tiny text helpers replacing the handful of CRT functions the app used.
#pragma once

#include <cstddef>
#include <string>

namespace tu {

// Case-insensitive comparison of two wide strings (like _wcsicmp).
int compareNoCase(const wchar_t* a, const wchar_t* b);

// Exact comparison (like std::wstring comparison, avoids STL vector helpers).
int compare(const wchar_t* a, const wchar_t* b);

// Substring search (like std::wstring::find).
bool contains(const wchar_t* haystack, const wchar_t* needle);

// Index of the last '\\' or '/', or std::wstring::npos.
std::wstring::size_type lastPathSeparator(const std::wstring& path);

// Tests whether a character belongs to a literal set.
bool containsChar(const wchar_t* set, wchar_t value);

// Bounded copy, always NUL-terminated (like wcsncpy_s with _TRUNCATE).
void copy(wchar_t* destination, std::size_t capacity, const wchar_t* source);

// Tolerant integer parse (like _wtoi).
int toInt(const wchar_t* text);

// Writes an unsigned value as decimal into buffer, returns buffer.
wchar_t* formatUnsigned(wchar_t* buffer, std::size_t capacity, unsigned long value);

}  // namespace tu
