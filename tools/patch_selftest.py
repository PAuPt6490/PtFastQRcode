# -*- coding: utf-8 -*-
"""Adds a headless self-test mode so the QR-to-PNG path can be exercised without UI."""
import io

MAIN = r"C:\PtFastQRcode\src\app\main.cpp"
text = io.open(MAIN, encoding="utf-8-sig").read()


def sub(old, new, label):
    global text
    if old not in text:
        raise SystemExit("MISSING: " + label)
    text = text.replace(old, new, 1)


SELFTEST = '''// Headless check of the encode-and-save path: encodes a fixed string and writes it as
// PNG. Used to verify the WIC path on machines where the UI cannot be driven.
static int runSelfTest(const std::wstring& arguments) {
    const wchar_t* cursor = arguments.c_str();
    takeQuotedArgument(cursor);  // --selftest
    const std::wstring target = takeQuotedArgument(cursor);
    if (target.empty()) return 2;

    const std::wstring report = target + L".txt";
    std::wstring result;

    qr::Symbol symbol;
    std::wstring error;
    qr::EncodeOptions options;
    options.ecc = qr::Ecc::Medium;
    if (!qr::encodeText(L"https://example.com/selftest", options, symbol, error)) {
        result = L"encode-failed: " + error;
    } else {
        result = L"symbol=" + std::to_wstring(symbol.size);
        const bool saved = saveQrPng(symbol, 360, target);
        result += saved ? L" save=ok" : L" save=failed";
        HANDLE file = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size = {};
            GetFileSizeEx(file, &size);
            CloseHandle(file);
            result += L" bytes=" + std::to_wstring(size.QuadPart);
        } else {
            result += L" file=missing";
        }
    }

    HANDLE out = CreateFileW(report.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(out, result.c_str(), (DWORD)(result.size() * sizeof(wchar_t)), &written, nullptr);
        CloseHandle(out);
    }
    return 0;
}

'''

sub("// Reads one quoted argument starting at cursor; advances the cursor past it.",
    SELFTEST + "// Reads one quoted argument starting at cursor; advances the cursor past it.",
    "selftest function")

sub('    if (tu::contains(arguments.c_str(), L"--pick-save")) return runSaveDialogHelper(arguments);',
    '    if (tu::contains(arguments.c_str(), L"--pick-save")) return runSaveDialogHelper(arguments);\n'
    '    if (tu::contains(arguments.c_str(), L"--selftest")) return runSelfTest(arguments);',
    "dispatch")

io.open(MAIN, "w", encoding="utf-8", newline="\n").write(text)
print("main.cpp: --selftest mode added")
