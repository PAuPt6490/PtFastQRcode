# -*- coding: utf-8 -*-
"""Run the save dialog in a short-lived child process so shell extensions never
land in the long-lived tray process."""
import io

POPUP = r"C:\PtFastQRcode\src\app\popup.cpp"
MAIN = r"C:\PtFastQRcode\src\app\main.cpp"
APPH = r"C:\PtFastQRcode\src\app\app.h"


def patch(path, pairs):
    text = io.open(path, encoding="utf-8-sig").read()
    for old, new in pairs:
        if old not in text:
            raise SystemExit("MISSING in %s:\n%s" % (path, old[:180]))
        text = text.replace(old, new, 1)
    io.open(path, "w", encoding="utf-8", newline="\n").write(text)
    print("patched", path.split("\\")[-1])


# ---------------------------------------------------------------- app.h
patch(APPH, [
    ("""// Renders the symbol into a 32bpp DIB and writes it as PNG through WIC.""",
     """// Shows the save dialog in a helper process (same exe, --pick-save) so the shell
// extensions it loads never stay resident in this one.
bool pickSavePathInChild(const std::wstring& initialDir, const std::wstring& suggestedName,
                         std::wstring& chosenPath);

// Renders the symbol into a 32bpp DIB and writes it as PNG through WIC."""),
])

# ---------------------------------------------------------------- popup.cpp
old_picker = """// comdlg32 is loaded on first save instead of at startup: one DLL less in the tray-only state.
typedef BOOL(WINAPI* GetSaveFileNameWFn)(LPOPENFILENAMEW);

BOOL pickSavePath(OPENFILENAMEW* dialog) {
    static GetSaveFileNameWFn resolve = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        HMODULE module = LoadLibraryW(L"comdlg32.dll");
        if (module) resolve = (GetSaveFileNameWFn)GetProcAddress(module, "GetSaveFileNameW");
    }
    return resolve && resolve(dialog);
}
"""
new_picker = """// The common dialog pulls Explorer shell extensions into whichever process opens it and
// COM keeps them loaded (~10 MB that never comes back). So the dialog runs in a
// short-lived copy of this executable instead, and only the chosen path comes back.
bool pickSavePathInChild(const std::wstring& initialDir, const std::wstring& suggestedName,
                         std::wstring& chosenPath) {
    wchar_t temporary[MAX_PATH] = {0};
    if (!GetTempPathW(MAX_PATH, temporary)) return false;
    wchar_t resultPath[MAX_PATH] = {0};
    if (!GetTempFileNameW(temporary, L"ptq", 0, resultPath)) return false;

    wchar_t module[MAX_PATH] = {0};
    if (!GetModuleFileNameW(nullptr, module, MAX_PATH)) {
        DeleteFileW(resultPath);
        return false;
    }

    std::wstring commandLine = L"\\"" + std::wstring(module) + L"\\" --pick-save \\"" +
                               std::wstring(resultPath) + L"\\" \\"" + initialDir + L"\\" \\"" +
                               suggestedName + L"\\"";

    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process = {};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(0);

    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) {
        DeleteFileW(resultPath);
        return false;
    }

    AllowSetForegroundWindow(process.dwProcessId);

    // Keep pumping messages: the popup stays responsive and cannot be torn down while the
    // dialog is up (callers set suppressDeactivate).
    for (;;) {
        DWORD wait = MsgWaitForMultipleObjects(1, &process.hProcess, FALSE, INFINITE, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_OBJECT_0 + 1) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        } else {
            break;
        }
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    bool picked = false;
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
    return picked;
}
"""
patch(POPUP, [(old_picker, new_picker)])

patch(POPUP, [
    ("""    std::vector<wchar_t> buffer(MAX_PATH * 4, 0);
    tu::copy(buffer.data(), buffer.size(), suggested.c_str());

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter = L"PNG 图片 (*.png)\\0*.png\\0所有文件 (*.*)\\0*.*\\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = (DWORD)buffer.size();
    dialog.lpstrDefExt = L"png";
    dialog.lpstrInitialDir = directory.c_str();
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    st->suppressDeactivate = true;
    const BOOL accepted = pickSavePath(&dialog);
    st->suppressDeactivate = false;
    if (!accepted) return;

    std::wstring path = buffer.data();""",
     """    st->suppressDeactivate = true;
    std::wstring path;
    const bool accepted = pickSavePathInChild(directory, defaultFileNameFor(text), path);
    st->suppressDeactivate = false;
    if (!accepted) return;
"""),
])

# ---------------------------------------------------------------- main.cpp
patch(MAIN, [
    ("""        bool startHidden = tu::contains(arguments.c_str(), L"--startup");""",
     """        // Helper mode: open the save dialog here, hand the chosen path back, exit.
        if (tu::contains(arguments.c_str(), L"--pick-save")) {
            return runSaveDialogHelper(arguments);
        }

        bool startHidden = tu::contains(arguments.c_str(), L"--startup");"""),
])

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

// Save-dialog helper: runs in its own process so the shell extensions it loads are
// released when it exits. Writes the chosen path (UTF-16) to the result file.
static int runSaveDialogHelper(const std::wstring& arguments) {
    const wchar_t* cursor = arguments.c_str();
    takeQuotedArgument(cursor);                       // --pick-save
    const std::wstring resultPath = takeQuotedArgument(cursor);
    const std::wstring initialDir = takeQuotedArgument(cursor);
    const std::wstring suggestedName = takeQuotedArgument(cursor);
    if (resultPath.empty()) return 1;

    ensureCom();

    std::vector<wchar_t> buffer(MAX_PATH * 4, 0);
    const std::wstring initial = initialDir.empty() ? suggestedName : initialDir + L"\\\\" + suggestedName;
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
io.open(MAIN, "w", encoding="utf-8", newline="\n").write(text)
print("patched main.cpp")
