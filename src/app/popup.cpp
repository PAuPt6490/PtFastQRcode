#include "app.h"

#include "text_util.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <string>

#include "resource.h"
#include "net/httpd.h"

namespace {

constexpr int IDC_EDIT = 2001;
constexpr int IDC_BTN_SAVE = ID_POPUP_SAVE;
constexpr int IDC_BTN_COPY = ID_POPUP_COPY;
constexpr int IDC_BTN_PASTE = 2004;
constexpr int IDC_BTN_SETTINGS = 2005;
constexpr int IDC_BTN_PICK = 2007;
constexpr int IDC_BTN_CLOSE = ID_POPUP_CLOSE;

constexpr UINT TIMER_DEBOUNCE = 1;
constexpr UINT TIMER_STATUS = 2;
constexpr UINT TIMER_HIDE = 3;

const BtnSpec kButtons[] = {
    {IDC_BTN_SAVE, BtnKind::Primary, L"保存图片"},
    {IDC_BTN_COPY, BtnKind::Secondary, L"复制图片"},
    {IDC_BTN_PASTE, BtnKind::Secondary, L"粘贴"},
    {IDC_BTN_PICK, BtnKind::Secondary, L"选图"},
    {IDC_BTN_SETTINGS, BtnKind::IconSliders, L""},
    {IDC_BTN_CLOSE, BtnKind::IconClose, L""},
};

struct Layout {
    RECT headerIcon;
    RECT title;
    RECT subtitle;
    RECT separator;
    RECT contentLabel;
    RECT field;       // painted rounded field
    RECT edit;        // real EDIT control
    RECT card;
    RECT status;
    RECT hint;
    RECT btnSave;
    RECT btnCopy;
    RECT btnPaste;
    RECT btnPick;
    RECT btnSettings;
    RECT btnClose;
};

struct PopupState {
    HWND edit = nullptr;
    HWND btnSave = nullptr;
    HWND btnCopy = nullptr;
    HWND btnPaste = nullptr;
    HWND btnPick = nullptr;
    HWND btnSettings = nullptr;
    HWND btnClose = nullptr;

    HICON icon = nullptr;
    HBRUSH editBrush = nullptr;

    qr::Symbol symbol;
    bool hasSymbol = false;
    std::wstring error;

    std::wstring message;
    bool messageIsError = false;
    bool suppressDeactivate = false;

    // Set when the field holds a link to a picture this process is serving: the card then
    // shows a thumbnail of that picture next to the code.
    bool pictureMode = false;
    std::wstring pictureUrl;
    PicturePreview preview = {};

    // Directory of the last picture chosen from disk, so the file picker reopens there.
    std::wstring imageDir;

    Layout layout;
};

PopupState* getState(HWND hwnd) {
    return reinterpret_cast<PopupState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

const BtnSpec* findSpec(int id) {
    for (const BtnSpec& spec : kButtons) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

std::wstring getEditText(HWND edit) {
    int length = GetWindowTextLengthW(edit);
    if (length <= 0) return std::wstring();
    std::wstring text((size_t)length + 1, L'\0');
    int copied = GetWindowTextW(edit, &text[0], length + 1);
    text.resize((size_t)std::max(0, copied));
    return text;
}

std::wstring getClipboardText() {
    std::wstring result;
    if (!OpenClipboard(g_popup)) return result;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const wchar_t* text = (const wchar_t*)GlobalLock(data);
        if (text) {
            result = text;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return result;
}

std::wstring defaultStatusText() {
    return g_settings.closeOnBlur ? L"点击窗口外自动关闭  ·  Esc 退出"
                                  : L"点击窗口外不关闭  ·  Esc 退出";
}

std::wstring pixelSizeText(int width, int height) {
    if (width <= 0 || height <= 0) return std::wstring();
    wchar_t left[16];
    wchar_t right[16];
    tu::formatUnsigned(left, 16, (unsigned long)width);
    tu::formatUnsigned(right, 16, (unsigned long)height);
    return std::wstring(left) + L"×" + right;
}

// Runs a copy of this executable that shows one dialog and writes the chosen path to a
// temp file, then returns that path. Shared by the save dialog and the picture picker.
bool runPickerChild(const std::wstring& commandLine, std::vector<wchar_t>& resultPath,
                    std::wstring& chosenPath) {
    std::vector<wchar_t> module(MAX_PATH, 0);
    if (!GetModuleFileNameW(nullptr, module.data(), MAX_PATH)) {
        DeleteFileW(resultPath.data());
        return false;
    }

    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process = {};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(0);

    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) {
        DeleteFileW(resultPath.data());
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
    return picked;
}

bool makeResultPath(std::vector<wchar_t>& resultPath) {
    std::vector<wchar_t> temporary(MAX_PATH, 0);
    if (!GetTempPathW(MAX_PATH, temporary.data())) return false;
    resultPath.assign(MAX_PATH, 0);
    return GetTempFileNameW(temporary.data(), L"ptq", 0, resultPath.data()) != FALSE;
}

std::wstring modulePath() {
    std::vector<wchar_t> module(MAX_PATH, 0);
    if (!GetModuleFileNameW(nullptr, module.data(), MAX_PATH)) return std::wstring();
    return std::wstring(module.data());
}

// The common dialog pulls Explorer shell extensions into whichever process opens it and
// COM keeps them loaded (~10 MB that never comes back). So the dialog runs in a
// short-lived copy of this executable instead, and only the chosen path comes back.
bool pickSavePathInChild(const std::wstring& initialDir, const std::wstring& suggestedName,
                         std::wstring& chosenPath) {
    std::vector<wchar_t> resultPath;
    if (!makeResultPath(resultPath)) return false;

    const std::wstring module = modulePath();
    if (module.empty()) {
        DeleteFileW(resultPath.data());
        return false;
    }

    const std::wstring commandLine = L"\"" + module + L"\" --pick-save \"" +
                                     std::wstring(resultPath.data()) + L"\" \"" + initialDir +
                                     L"\" \"" + suggestedName + L"\"";
    return runPickerChild(commandLine, resultPath, chosenPath);
}

// Same trick for choosing a picture file to serve: the Explorer picker loads the same shell
// extensions, so it opens in a throwaway copy of this process.
bool pickImagePathInChild(const std::wstring& initialDir, std::wstring& chosenPath) {
    std::vector<wchar_t> resultPath;
    if (!makeResultPath(resultPath)) return false;

    const std::wstring module = modulePath();
    if (module.empty()) {
        DeleteFileW(resultPath.data());
        return false;
    }

    const std::wstring commandLine = L"\"" + module + L"\" --pick-image \"" +
                                     std::wstring(resultPath.data()) + L"\" \"" + initialDir +
                                     L"\"";
    return runPickerChild(commandLine, resultPath, chosenPath);
}

std::wstring baseName(const std::wstring& path) {
    size_t slash = tu::lastPathSeparator(path);
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring directoryOf(const std::wstring& path) {
    size_t slash = tu::lastPathSeparator(path);
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

constexpr int kWidth = 400;
constexpr int kHeight = 470;
constexpr int kMinWidth = 340;
constexpr int kMinHeight = 400;

// The popup is freely resizable: the header, text field and buttons keep their heights,
// while the QR card takes every remaining pixel.
void layoutControls(HWND hwnd, PopupState* st) {
    RECT client = {0, 0, S(kWidth), S(kHeight)};
    GetClientRect(hwnd, &client);
    const int width = client.right;
    const int height = client.bottom;
    Layout& l = st->layout;

    l.headerIcon = {S(14), S(12), S(38), S(36)};
    l.title = {S(46), S(4), width - S(100), S(26)};
    l.subtitle = {S(46), S(24), width - S(100), S(44)};
    l.separator = {S(14), S(50), width - S(14), S(51)};

    l.contentLabel = {S(14), S(54), width - S(14), S(68)};
    l.field = {S(14), S(70), width - S(14), S(128)};

    // The picture picker belongs to the input, not to the action row: it sits at the right
    // end of the text field, like a browse button next to a file name box.
    const int pickWidth = S(48);
    l.btnPick = {width - S(14) - S(7) - pickWidth, S(78), width - S(14) - S(7), S(120)};
    l.edit = {S(21), S(77), l.btnPick.left - S(8), S(121)};

    l.card = {S(14), S(134), width - S(14), height - S(100)};
    if (l.card.bottom < l.card.top + S(80)) l.card.bottom = l.card.top + S(80);

    l.status = {S(16), height - S(96), width - S(16), height - S(78)};
    l.hint = {0, height - S(30), width, height - S(14)};

    const int buttonTop = height - S(72);
    const int buttonBottom = height - S(36);
    // Three action buttons share the row. The widths are proportional so the row still fits
    // the narrowest window size without any label being clipped.
    const int gap = S(8);
    const int usable = (width - S(28)) - gap * 2;
    int x = S(14);
    auto nextButton = [&](float weight) {
        const int w = (int)(usable * weight);
        RECT r = {x, buttonTop, x + w, buttonBottom};
        x += w + gap;
        return r;
    };
    l.btnSave = nextButton(0.32f);
    l.btnCopy = nextButton(0.32f);
    l.btnPaste = {x, buttonTop, width - S(14), buttonBottom};

    l.btnSettings = {width - S(82), S(10), width - S(52), S(40)};
    l.btnClose = {width - S(46), S(10), width - S(16), S(40)};

    auto place = [&](HWND control, const RECT& r) {
        if (control)
            SetWindowPos(control, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
    };
    place(st->edit, l.edit);
    place(st->btnSave, l.btnSave);
    place(st->btnCopy, l.btnCopy);
    place(st->btnPaste, l.btnPaste);
    place(st->btnPick, l.btnPick);
    place(st->btnSettings, l.btnSettings);
    place(st->btnClose, l.btnClose);
}

void setStatus(HWND hwnd, PopupState* st, const std::wstring& text, bool isError) {
    st->message = text;
    st->messageIsError = isError;
    InvalidateRect(hwnd, &st->layout.status, FALSE);
    if (!text.empty()) SetTimer(hwnd, TIMER_STATUS, 3200, nullptr);
}

void refreshQr(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (!st) return;

    st->hasSymbol = false;
    st->error.clear();

    std::wstring text = getEditText(st->edit);
    if (st->pictureMode && tu::compare(text.c_str(), st->pictureUrl.c_str()) != 0) {
        // The link was edited or replaced: the thumbnail no longer describes the content.
        st->pictureMode = false;
        st->pictureUrl.clear();
        releasePicturePreview(st->preview);
    }
    if (!text.empty()) {
        qr::EncodeOptions options;
        options.ecc = (qr::Ecc)std::max(0, std::min(3, g_settings.eccLevel));
        std::wstring error;
        if (qr::encodeText(text, options, st->symbol, error)) {
            st->hasSymbol = true;
        } else {
            st->error = error.empty() ? L"无法生成二维码" : error;
        }
    }
    InvalidateRect(hwnd, &st->layout.card, FALSE);
}

// The code keeps the middle of the card, so in picture mode the thumbnails goes into the
// gutter the quiet zone leaves free on the left, with the two sizes that matter below it.
// Everything is measured off the rectangle drawQrSymbol() works with, so both stay aligned
// at any window size and the thumbnail can never land on the modules.
void drawPreviewThumb(HDC hdc, const RECT& card, const PopupState* st) {
    const PicturePreview& preview = st->preview;
    if (!preview.bitmap || preview.width <= 0 || preview.height <= 0) return;

    const int cardWidth = card.right - card.left;
    const int cardHeight = card.bottom - card.top;
    if (cardWidth <= 0 || cardHeight <= 0 || st->symbol.size <= 0) return;

    int side = (cardWidth < cardHeight ? cardWidth : cardHeight) - S(28);
    if (side < 8) side = cardWidth < cardHeight ? cardWidth : cardHeight;
    const int modules = st->symbol.size + 8;
    int module = side / modules;
    if (module < 1) module = 1;
    const int drawn = module * modules;
    const int gutter = (cardWidth - drawn) / 2;

    const int captionBlock = S(38);
    const int maxWidth = gutter - S(10);
    const int maxHeight = cardHeight - captionBlock - S(10);
    if (maxWidth < S(44) || maxHeight < S(36)) return;

    float scale = (float)maxWidth / (float)preview.width;
    const float byHeight = (float)maxHeight / (float)preview.height;
    if (byHeight < scale) scale = byHeight;
    int thumbWidth = (int)(preview.width * scale + 0.5f);
    int thumbHeight = (int)(preview.height * scale + 0.5f);
    if (thumbWidth < 1) thumbWidth = 1;
    if (thumbHeight < 1) thumbHeight = 1;

    RECT box;
    box.left = card.left + (gutter - thumbWidth) / 2;
    box.top = card.top + (cardHeight - captionBlock - thumbHeight) / 2;
    if (box.top < card.top + S(6)) box.top = card.top + S(6);
    box.right = box.left + thumbWidth;
    box.bottom = box.top + thumbHeight;

    HDC memory = CreateCompatibleDC(hdc);
    if (memory) {
        HGDIOBJ previous = SelectObject(memory, preview.bitmap);
        if (previous && previous != HGDI_ERROR) {
            BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            GdiAlphaBlend(hdc, box.left, box.top, thumbWidth, thumbHeight, memory, 0, 0,
                          preview.width, preview.height, blend);
            SelectObject(memory, previous);
        }
        DeleteDC(memory);
    }
    drawRoundRect(hdc, box, S(4), CLR_NONE, g_theme.cardBorder, 1);

    const int captionLeft = card.left + S(4);
    const int captionRight = card.left + gutter - S(4);
    if (captionRight - captionLeft < S(30)) return;

    std::wstring source = pixelSizeText(preview.sourceWidth, preview.sourceHeight);
    RECT first = {captionLeft, box.bottom + S(6), captionRight, box.bottom + S(22)};
    drawTextGdi(hdc, g_fonts.hint, g_theme.textDim, source.c_str(), first,
                DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    std::wstring served = L"→ " + pixelSizeText(preview.linkWidth, preview.linkHeight);
    RECT second = {captionLeft, first.bottom, captionRight, first.bottom + S(16)};
    drawTextGdi(hdc, g_fonts.hint, g_theme.textDim, served.c_str(), second,
                DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// Paints only the parts that intersect the invalidated rectangle: when a status line
// changes there is no reason to redraw the QR matrix on top of it.
void paintPopup(HWND hwnd, HDC hdc, const RECT& dirty) {
    PopupState* st = getState(hwnd);
    if (!st) return;

    RECT client;
    GetClientRect(hwnd, &client);

    paintWindowBackdrop(hdc, client, true);

    const bool headerDirty = rectsOverlap(dirty, st->layout.separator);
    const bool fieldDirty = rectsOverlap(dirty, st->layout.field);
    const bool cardDirty = rectsOverlap(dirty, st->layout.card);
    const bool statusDirty = rectsOverlap(dirty, st->layout.status);

    if (headerDirty && st->icon) {
        DrawIconEx(hdc, st->layout.headerIcon.left, st->layout.headerIcon.top, st->icon,
                   st->layout.headerIcon.right - st->layout.headerIcon.left,
                   st->layout.headerIcon.bottom - st->layout.headerIcon.top, 0, nullptr,
                   DI_NORMAL);
    }

    if (headerDirty) {
        RECT titleRect = st->layout.title;
        drawTextGdi(hdc, g_fonts.title, g_theme.text, L"PtFastQRcode", titleRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        std::wstring subtitle =
            formatHotkey(g_settings.hotkeyMods, g_settings.hotkeyVk) + L"  唤起";
        RECT subtitleRect = st->layout.subtitle;
        drawTextGdi(hdc, g_fonts.hint, g_theme.textDim, subtitle.c_str(), subtitleRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        HPEN pen = CreatePen(PS_SOLID, 1, g_theme.cardBorder);
        HGDIOBJ previous = SelectObject(hdc, pen);
        MoveToEx(hdc, st->layout.separator.left, st->layout.separator.top, nullptr);
        LineTo(hdc, st->layout.separator.right + 1, st->layout.separator.top);
        SelectObject(hdc, previous);
        DeleteObject(pen);
    }

    if (fieldDirty) {
        RECT labelRect = st->layout.contentLabel;
        drawTextGdi(hdc, g_fonts.hint, g_theme.textDim, L"内容", labelRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        paintFieldFrame(hwnd, hdc, st->layout.field, g_theme.radius);
    }

    // ---- QR card ----
    if (cardDirty) {
        drawRoundRect(hdc, st->layout.card, g_theme.radius + 2, g_theme.card, g_theme.cardBorder, 1);

        RECT card = st->layout.card;
        if (st->hasSymbol) {
            drawQrSymbol(hdc, card, st->symbol, S(28));
            if (st->pictureMode) drawPreviewThumb(hdc, card, st);
        } else {
            std::wstring placeholder = st->error.empty() ? L"粘贴或输入内容后自动生成" : st->error;
            RECT textRect = card;
            textRect.left += S(24);
            textRect.right -= S(24);
            drawTextGdi(hdc, g_fonts.body,
                        st->error.empty() ? g_theme.textDim : g_theme.danger, placeholder.c_str(),
                        textRect, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }
    }

    if (statusDirty) {
        std::wstring status = st->message.empty() ? defaultStatusText() : st->message;
        COLORREF statusColor = st->messageIsError
                                   ? g_theme.danger
                                   : (st->message.empty() ? g_theme.textDim : g_theme.text);
        RECT statusRect = st->layout.status;
        drawTextGdi(hdc, g_fonts.hint, statusColor, status.c_str(), statusRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (rectsOverlap(dirty, st->layout.hint)) {
        RECT hintRect = st->layout.hint;
        std::wstring hint = formatHotkey(g_settings.closeMods, g_settings.closeVk) + L" 关闭    " +
                            formatHotkey(g_settings.saveMods, g_settings.saveVk) + L" 保存    " +
                            formatHotkey(g_settings.copyMods, g_settings.copyVk) + L" 复制图片";
        drawTextGdi(hdc, g_fonts.hint, g_theme.textDim, hint.c_str(), hintRect,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void savePng(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (!st) return;
    if (!st->hasSymbol) {
        setStatus(hwnd, st, L"请先输入要生成二维码的内容", true);
        return;
    }

    std::wstring text = getEditText(st->edit);
    std::wstring directory = g_settings.saveDir;
    ensureDirectory(directory);
    std::wstring suggested = directory + L"\\" + defaultFileNameFor(text);

    st->suppressDeactivate = true;
    std::wstring path;
    const bool accepted = pickSavePathInChild(directory, defaultFileNameFor(text), path);
    st->suppressDeactivate = false;
    if (!accepted) return;

    const bool saved = saveQrPng(st->symbol, S(g_settings.qrSize), path);

    if (saved) {
        g_settings.saveDir = directoryOf(path);
        saveSettings();
        setStatus(hwnd, st, L"已保存 " + baseName(path), false);
        if (g_settings.closeAfterSave) SetTimer(hwnd, TIMER_HIDE, 700, nullptr);
    } else {
        setStatus(hwnd, st, L"保存失败,请检查目录权限", true);
    }
}

void copyImage(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (!st) return;
    if (!st->hasSymbol) {
        setStatus(hwnd, st, L"请先输入要生成二维码的内容", true);
        return;
    }
    const bool copied = copyQrToClipboard(hwnd, st->symbol, S(g_settings.qrSize));
    setStatus(hwnd, st, copied ? L"二维码图片已复制到剪贴板" : L"复制失败,剪贴板被占用", !copied);
}

// A picture cannot ride in a QR code, so with the LAN server on it becomes a link that the
// phone can open; the picture itself lives in memory on the server for the configured
// lifetime. Both sources (clipboard, file) land here.
bool publishPictureSource(HWND hwnd, const std::wstring& filePath, bool fromFile) {
    PopupState* st = getState(hwnd);
    if (!st) return false;

    st->hasSymbol = false;
    st->error.clear();
    st->pictureMode = false;
    st->pictureUrl.clear();
    releasePicturePreview(st->preview);

    if (!g_settings.imageServer) {
        SetWindowTextW(st->edit, L"");
        setStatus(hwnd, st, L"图片需要先在设置里打开「图片链接」", true);
        InvalidateRect(hwnd, &st->layout.card, FALSE);
        return true;
    }

    std::wstring url;
    // The server is opened here, on the first picture, and closed again by the controller
    // once the links have expired.
    net::setLifetime(g_settings.linkMinutes);
    if (!net::running()) net::start(g_controller);
    if (!net::running()) {
        setStatus(hwnd, st, L"本机没有可用的局域网地址", true);
        return true;
    }

    const bool published = fromFile ? publishFilePicture(filePath, url, st->preview)
                                    : publishClipboardPicture(url, st->preview);
    // Handling a full size picture leaves a few megabytes of freed pages behind; give them
    // back right away instead of keeping them for as long as the popup stays open.
    trimProcessMemory();
    if (!published) {
        setStatus(hwnd, st, fromFile ? L"打不开这张图片" : L"图片链接生成失败", true);
        return true;
    }

    // The link is the QR content, so it shows up in the field and the symbol is rebuilt
    // from it: scanning the QR code opens the page that shows the picture.
    SetWindowTextW(st->edit, url.c_str());
    st->pictureMode = true;
    st->pictureUrl = url;
    refreshQr(hwnd);
    wchar_t minutes[16];
    tu::formatUnsigned(minutes, 16, (unsigned long)g_settings.linkMinutes);
    std::wstring status = L"已生成图片链接(";
    status += minutes;
    status += L" 分钟内有效)";
    setStatus(hwnd, st, status.c_str(), false);
    return true;
}

bool tryPublishPicture(HWND hwnd) {
    if (!clipboardHasPicture()) return false;
    return publishPictureSource(hwnd, std::wstring(), false);
}

// Manual route: pick a picture file, serve it exactly like a pasted one.
void pickPictureFile(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (!st) return;

    std::wstring initial = st->imageDir;
    if (initial.empty()) initial = g_settings.saveDir;

    st->suppressDeactivate = true;
    std::wstring path;
    const bool picked = pickImagePathInChild(initial, path);
    st->suppressDeactivate = false;
    if (!picked) return;

    st->imageDir = directoryOf(path);
    if (!publishPictureSource(hwnd, path, true)) {
        setStatus(hwnd, st, L"图片链接生成失败", true);
    }
}

void pasteFromClipboard(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (!st) return;
    if (tryPublishPicture(hwnd)) return;
    std::wstring text = getClipboardText();
    if (text.empty()) {
        setStatus(hwnd, st, L"剪贴板中没有文本", true);
        return;
    }
    SetWindowTextW(st->edit, text.c_str());
    SendMessageW(st->edit, EM_SETSEL, 0, -1);
    SetFocus(st->edit);
    refreshQr(hwnd);
    setStatus(hwnd, st, L"已从剪贴板粘贴", false);
}

// Hiding also destroys the window: a hidden popup would otherwise keep its edit control,
// fonts and layout strings resident, so returning to the tray never fell back fully.
// The window is hidden immediately (no flicker) and destroyed from the message loop.
void hideWindow(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (st) {
        KillTimer(hwnd, TIMER_DEBOUNCE);
        KillTimer(hwnd, TIMER_STATUS);
        KillTimer(hwnd, TIMER_HIDE);
        st->message.clear();
        st->suppressDeactivate = true;
    }
    ShowWindow(hwnd, SW_HIDE);
    PostMessageW(hwnd, WM_APP_HIDE, 0, 0);
}

LRESULT CALLBACK PopupProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    PopupState* st = getState(hwnd);

    switch (message) {
        case WM_CREATE: {
            st = new PopupState();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

            st->icon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, S(26),
                                         S(26), LR_DEFAULTCOLOR);
            st->editBrush = CreateSolidBrush(g_theme.fieldBg);

            st->edit = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL |
                    WS_VSCROLL,
                S(25), S(87), S(371), S(46), hwnd, (HMENU)(INT_PTR)IDC_EDIT, g_hinst, nullptr);
            SendMessageW(st->edit, WM_SETFONT, (WPARAM)g_fonts.body, TRUE);
            SendMessageW(st->edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(S(2), S(4)));

            struct {
                HWND* handle;
                int id;
                const wchar_t* text;
            } buttons[] = {
                {&st->btnSave, IDC_BTN_SAVE, L"保存图片"},
                {&st->btnCopy, IDC_BTN_COPY, L"复制图片"},
                {&st->btnPaste, IDC_BTN_PASTE, L"粘贴"},
                {&st->btnPick, IDC_BTN_PICK, L"选图"},
                {&st->btnSettings, IDC_BTN_SETTINGS, L""},
                {&st->btnClose, IDC_BTN_CLOSE, L""},
            };
            for (auto& button : buttons) {
                *button.handle = CreateWindowExW(
                    0, L"BUTTON", button.text,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hwnd,
                    (HMENU)(INT_PTR)button.id, g_hinst, nullptr);
                SendMessageW(*button.handle, WM_SETFONT, (WPARAM)g_fonts.bold, TRUE);
                installButtonSubclass(*button.handle);
            }

            layoutControls(hwnd, st);
            applyWindowShape(hwnd);
            return 0;
        }

        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE && st && !st->suppressDeactivate &&
                g_settings.closeOnBlur) {
                hideWindow(hwnd);
            }
            break;
        }

        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, message, wParam, lParam);
            if (hit != HTCLIENT) return hit;
            POINT local = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &local);
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int grip = S(6);
            bool left = local.x < grip;
            bool right = local.x >= rc.right - grip;
            bool top = local.y < grip;
            bool bottom = local.y >= rc.bottom - grip;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            if (local.y < S(50)) return HTCAPTION;
            return HTCLIENT;
        }

        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = (MINMAXINFO*)lParam;
            info->ptMinTrackSize.x = S(kMinWidth);
            info->ptMinTrackSize.y = S(kMinHeight);
            return 0;
        }

        case WM_SIZE: {
            if (st && wParam != SIZE_MINIMIZED) {
                layoutControls(hwnd, st);
                applyWindowShape(hwnd);
                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
            }
            return 0;
        }

        case WM_ENTERSIZEMOVE:
            setPaintBuffered(true);
            return 0;

        case WM_EXITSIZEMOVE: {
            setPaintBuffered(false);
            InvalidateRect(hwnd, nullptr, FALSE);
            RECT rc;
            GetWindowRect(hwnd, &rc);
            int width = (int)((rc.right - rc.left) / g_scale + 0.5f);
            int height = (int)((rc.bottom - rc.top) / g_scale + 0.5f);
            if (width != g_settings.popupWidth || height != g_settings.popupHeight) {
                g_settings.popupWidth = width;
                g_settings.popupHeight = height;
                saveSettings();
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (paintIsBuffered()) {
                RECT client;
                GetClientRect(hwnd, &client);
                PaintBuffer buffer = beginPaintBuffer(hwnd, client);
                paintPopup(hwnd, buffer.dc, ps.rcPaint);
                endPaintBuffer(hwnd, buffer);
            } else {
                paintPopup(hwnd, dc, ps.rcPaint);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* dis = (const DRAWITEMSTRUCT*)lParam;
            const BtnSpec* spec = findSpec((int)dis->CtlID);
            if (spec) drawButtonSpec(dis, *spec, false);
            return TRUE;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, g_theme.text);
            SetBkColor(dc, g_theme.fieldBg);
            if (st && st->editBrush) return (LRESULT)st->editBrush;
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int notification = HIWORD(wParam);

            if (id == IDC_EDIT && notification == EN_CHANGE) {
                SetTimer(hwnd, TIMER_DEBOUNCE, 110, nullptr);
                return 0;
            }
            switch (id) {
                case IDC_BTN_SAVE:
                    savePng(hwnd);
                    return 0;
                case IDC_BTN_COPY:
                    copyImage(hwnd);
                    return 0;
                case IDC_BTN_PASTE:
                    pasteFromClipboard(hwnd);
                    return 0;
                case IDC_BTN_PICK:
                    pickPictureFile(hwnd);
                    return 0;
                case IDC_BTN_SETTINGS:
                    if (st) st->suppressDeactivate = true;
                    hideWindow(hwnd);
                    showSettingsWindow();
                    return 0;
                case IDC_BTN_CLOSE:
                    hideWindow(hwnd);
                    return 0;
                default:
                    break;
            }
            break;
        }

        case WM_TIMER: {
            if (wParam == TIMER_DEBOUNCE) {
                KillTimer(hwnd, TIMER_DEBOUNCE);
                refreshQr(hwnd);
                return 0;
            }
            if (wParam == TIMER_STATUS) {
                KillTimer(hwnd, TIMER_STATUS);
                if (st) {
                    st->message.clear();
                    st->messageIsError = false;
                    InvalidateRect(hwnd, &st->layout.status, FALSE);
                }
                return 0;
            }
            if (wParam == TIMER_HIDE) {
                KillTimer(hwnd, TIMER_HIDE);
                hideWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            hideWindow(hwnd);
            return 0;

        case WM_APP_HIDE:
            DestroyWindow(hwnd);
            trimProcessMemory();
            return 0;

        case WM_SETTINGCHANGE:
            if (wParam == SPI_SETWORKAREA) {
                // Multi-monitor or taskbar changes: nothing cached, safe to ignore.
            }
            break;

        case WM_DESTROY: {
            if (st) {
                if (st->icon) DestroyIcon(st->icon);
                if (st->editBrush) DeleteObject(st->editBrush);
                releasePicturePreview(st->preview);
                delete st;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            g_popup = nullptr;
            return 0;
        }

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

// Returns the pages freed by tearing the window down: compact the heap first so the
// commit charge actually drops, then empty the working set.
void trimProcessMemory() {
    // Declared locally: the SDK hides these behind a Windows 10 target version, but the
    // call is harmless on anything older (it simply fails).
    struct HeapOptimizeInfo {
        DWORD Version;
        DWORD Flags;
    } optimize = {1, 0};
    HeapSetInformation(GetProcessHeap(), (HEAP_INFORMATION_CLASS)3, &optimize, sizeof(optimize));
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

HWND createPopupWindow() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = PopupProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kWndClassPopup;
        wc.style = CS_DROPSHADOW;
        wc.hIcon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0);
        RegisterClassExW(&wc);
        registered = true;
    }

    ensureFonts();
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kWndClassPopup, kAppName,
                                WS_POPUP | WS_CLIPCHILDREN, 0, 0, S(g_settings.popupWidth), S(g_settings.popupHeight),
                                nullptr, nullptr, g_hinst,
                                nullptr);
    g_popup = hwnd;
    return hwnd;
}

void showPopup() {
    if (g_popup && IsWindowVisible(g_popup) && GetForegroundWindow() == g_popup) {
        hidePopup();
        return;
    }
    if (!g_popup) createPopupWindow();
    if (!g_popup) return;

    PopupState* st = getState(g_popup);
    if (!st) return;

    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO monitor = {sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);

    int width = S(g_settings.popupWidth);
    int height = S(g_settings.popupHeight);
    int x = cursor.x - width / 2;
    int y = cursor.y + S(18);
    if (y + height > monitor.rcWork.bottom) y = cursor.y - height - S(18);
    if (y < monitor.rcWork.top + S(8)) y = monitor.rcWork.top + S(8);
    int maxX = monitor.rcWork.right - width - S(8);
    if (x > maxX) x = maxX;
    if (x < monitor.rcWork.left + S(8)) x = monitor.rcWork.left + S(8);

    SetWindowPos(g_popup, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    ShowWindow(g_popup, SW_SHOW);
    SetForegroundWindow(g_popup);
    SetFocus(st->edit);

    if (g_settings.autoPaste) {
        std::wstring text = getClipboardText();
        if (!text.empty()) {
            std::wstring current = getEditText(st->edit);
            if (tu::compare(text.c_str(), current.c_str()) != 0)
                SetWindowTextW(st->edit, text.c_str());
        } else {
            tryPublishPicture(g_popup);
            return;
        }
    }
    SendMessageW(st->edit, EM_SETSEL, 0, -1);
    refreshQr(g_popup);
    InvalidateRect(g_popup, nullptr, FALSE);
}

void hidePopup() {
    if (g_popup) hideWindow(g_popup);
}

void refreshPopupContent(bool pasteFirst) {
    if (!g_popup) return;
    if (pasteFirst) {
        pasteFromClipboard(g_popup);
        return;
    }
    refreshQr(g_popup);
}

void popupSetStatus(const std::wstring& text, bool isError) {
    if (!g_popup) return;
    PopupState* st = getState(g_popup);
    if (st) setStatus(g_popup, st, text, isError);
}
