# -*- coding: utf-8 -*-
"""Wire the --pick-save helper into main.cpp and keep stack frames small (no _chkstk)."""
import io

MAIN = r"C:\PtFastQRcode\src\app\main.cpp"
POPUP = r"C:\PtFastQRcode\src\app\popup.cpp"


def patch(path, pairs):
    text = io.open(path, encoding="utf-8-sig").read()
    for old, new in pairs:
        if old not in text:
            raise SystemExit("MISSING in %s:\n%s" % (path, old[:200]))
        text = text.replace(old, new, 1)
    io.open(path, "w", encoding="utf-8", newline="\n").write(text)
    print("patched", path.split("\\")[-1])


# ---- main.cpp: helper function + dispatch -----------------------------------
text = io.open(MAIN, encoding="utf-8-sig").read()
anchor = "int WINAPI wWinMain("
helper = '''// Reads one quoted argument starting at cursor; advances the cursor past it.
static std::wstring takeQuotedArgument(const wchar_t*& cursor) {
    while (*cursor == L' ' || *cursor == L'\\t') ++cursor;
    std::wstring value;
    if (*cursor == L'"') {
        ++cursor;
        while (*cursor && *cursor != L'"') value.push_back(*cursor++);
        if (*cursor == L'"') ++cursor;
    } else {
        while (*cursor && *cursor != L' ' && *cursor != L'\\t') value.push_back(*cursor++);
    }
    return value;
}

// Save-dialog helper: runs in its own process so the shell extensions the common dialog
// loads are released the moment it exits. Writes the chosen path (UTF-16) to resultPath.
static int runSaveDialogHelper(const std::wstring& arguments) {
    const wchar_t* cursor = arguments.c_str();
    takeQuotedArgument(cursor);  // --pick-save
    const std::wstring resultPath = takeQuotedArgument(cursor);
    const std::wstring initialDir = takeQuotedArgument(cursor);
    const std::wstring suggestedName = takeQuotedArgument(cursor);
    if (resultPath.empty()) return 1;

    ensureCom();

    std::vector<wchar_t> buffer(MAX_PATH * 4, 0);
    const std::wstring initial =
        initialDir.empty() ? suggestedName : initialDir + L"\\\\" + suggestedName;
    tu::copy(buffer.data(), buffer.size(), initial.c_str());

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"PNG 图片 (*.png)\\0*.png\\0所有文件 (*.*)\\0*.*\\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = (DWORD)buffer.size();
    dialog.lpstrDefExt = L"png";
    dialog.lpstrInitialDir = initialDir.c_str();
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    const BOOL accepted = GetSaveFileNameW(&dialog);

    HANDLE file = CreateFileW(resultPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        if (accepted) {
            DWORD written = 0;
            WriteFile(file, buffer.data(), (DWORD)(wcslen(buffer.data()) * sizeof(wchar_t)),
                      &written, nullptr);
        }
        CloseHandle(file);
    }
    return 0;
}

'''
assert anchor in text
text = text.replace(anchor, helper + anchor, 1)
text = text.replace('''    std::wstring arguments = commandLine ? commandLine : L"";
    bool startHidden''',
                    '''    std::wstring arguments = commandLine ? commandLine : L"";
    if (tu::contains(arguments.c_str(), L"--pick-save")) return runSaveDialogHelper(arguments);
    bool startHidden''', 1)
io.open(MAIN, "w", encoding="utf-8", newline="\n").write(text)
print("patched main.cpp")

# ---- popup.cpp: heap buffers instead of a large stack frame -----------------
patch(POPUP, [
    ("""    wchar_t temporary[MAX_PATH] = {0};
    if (!GetTempPathW(MAX_PATH, temporary)) return false;
    wchar_t resultPath[MAX_PATH] = {0};
    if (!GetTempFileNameW(temporary, L"ptq", 0, resultPath)) return false;

    wchar_t module[MAX_PATH] = {0};
    if (!GetModuleFileNameW(nullptr, module, MAX_PATH)) {
        DeleteFileW(resultPath);
        return false;
    }""",
     """    std::vector<wchar_t> temporary(MAX_PATH, 0);
    if (!GetTempPathW(MAX_PATH, temporary.data())) return false;
    std::vector<wchar_t> resultPath(MAX_PATH, 0);
    if (!GetTempFileNameW(temporary.data(), L"ptq", 0, resultPath.data())) return false;

    std::vector<wchar_t> module(MAX_PATH, 0);
    if (!GetModuleFileNameW(nullptr, module.data(), MAX_PATH)) {
        DeleteFileW(resultPath.data());
        return false;
    }"""),

    ("""    std::wstring commandLine = L"\\"" + std::wstring(module) + L"\\" --pick-save \\"" +
                               std::wstring(resultPath) + L"\\" \\"" + initialDir + L"\\" \\"" +
                               suggestedName + L"\\"";""",
     """    std::wstring commandLine = L"\\"" + std::wstring(module.data()) + L"\\" --pick-save \\"" +
                               std::wstring(resultPath.data()) + L"\\" \\"" + initialDir + L"\\" \\"" +
                               suggestedName + L"\\"";"""),

    ("""    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) {
        DeleteFileW(resultPath);
        return false;
    }""",
     """    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) {
        DeleteFileW(resultPath.data());
        return false;
    }"""),

    ("""    bool picked = false;
    HANDLE file = CreateFileW(resultPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        wchar_t buffer[MAX_PATH * 4] = {0};
        DWORD read = 0;
        const DWORD limit = (DWORD)(sizeof(buffer) - sizeof(wchar_t));
        if (ReadFile(file, buffer, limit, &read, nullptr) && read > 0) {
            chosenPath.assign(buffer);
            picked = !chosenPath.empty();
        }
        CloseHandle(file);
    }
    DeleteFileW(resultPath);
    return picked;""",
     """    bool picked = false;
    HANDLE file = CreateFileW(resultPath.data(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        std::vector<wchar_t> buffer(MAX_PATH * 4, 0);
        DWORD read = 0;
        const DWORD limit = (DWORD)(buffer.size() * sizeof(wchar_t) - sizeof(wchar_t));
        if (ReadFile(file, buffer.data(), limit, &read, nullptr) && read > 0) {
            chosenPath.assign(buffer.data());
            picked = !chosenPath.empty();
        }
        CloseHandle(file);
    }
    DeleteFileW(resultPath.data());
    return picked;"""),
])
