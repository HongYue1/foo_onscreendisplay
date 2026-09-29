// Layout checker for the preferences dialog template: creates the real dialog from foo_osd.rc
// (no foobar2000 needed) and reports truncated text, controls that overlap on the same page and
// controls outside the dialog. Prints only problems and a summary. Build with test/build_test.bat.

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace {

struct Item {
    HWND hwnd;
    int id;
    int page; // -1 = common
    std::wstring cls;
    std::wstring text;
    RECT r;
    DWORD style;
};

INT_PTR CALLBACK dlgProc(HWND, UINT, WPARAM, LPARAM) { return FALSE; }

std::wstring className(HWND w) {
    wchar_t b[64] = {};
    GetClassNameW(w, b, 64);
    return b;
}

std::wstring windowText(HWND w) {
    wchar_t b[512] = {};
    GetWindowTextW(w, b, 512);
    return b;
}

int textHeight(HDC dc, HFONT font, const std::wstring& t, int width) {
    HGDIOBJ old = SelectObject(dc, font);
    RECT r{0, 0, width, 0};
    DrawTextW(dc, t.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    return r.bottom;
}

int textWidth(HDC dc, HFONT font, const std::wstring& t) {
    HGDIOBJ old = SelectObject(dc, font);
    SIZE s{};
    GetTextExtentPoint32W(dc, t.c_str(), static_cast<int>(t.size()), &s);
    SelectObject(dc, old);
    return s.cx;
}

const wchar_t* const kNames[] = {L"General", L"Appearance", L"Text", L"Fonts"};

std::vector<std::wstring> comboItems(int id) {
    switch (id) {
    case IDC_POSITION: return {L"Top left", L"Top center", L"Top right", L"Bottom left", L"Bottom center", L"Bottom right"};
    case IDC_LAYOUT: return {L"Classic", L"Compact", L"Banner", L"Poster"};
    case IDC_BG_MODE: return {L"Dark", L"Light", L"Cover tint", L"Cover colour", L"Custom"};
    case IDC_TEXT_MODE: return {L"Automatic", L"Custom"};
    case IDC_BORDER: return {L"None", L"Subtle", L"Accent"};
    case IDC_ART_SHAPE: return {L"Rounded", L"Circle", L"Square"};
    case IDC_ANIMATION: return {L"Fade", L"Slide", L"Glide", L"None"};
    case IDC_BAR_STYLE: return {L"Rounded", L"Thin", L"Thick"};
    case IDC_PRESET: return {L"Choose a preset...", L"Midnight (default)", L"Retro terminal", L"Big and bold"};
    case IDC_TITLE_FONT:
    case IDC_DETAIL_FONT: return {L"(Default)", L"Segoe UI Semibold"};
    default: return {};
    }
}

} // namespace

int wmain() {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"host";
    RegisterClassW(&wc);
    HWND host = CreateWindowW(L"host", L"host", WS_OVERLAPPEDWINDOW, 0, 0, 700, 600, nullptr, nullptr, wc.hInstance, nullptr);
    HWND dlg = CreateDialogParamW(wc.hInstance, MAKEINTRESOURCEW(IDD_OSD_PREFERENCES), host, dlgProc, 0);
    if (!dlg) {
        std::printf("CreateDialog failed: %lu\n", GetLastError());
        return 2;
    }

    RECT dr{};
    GetClientRect(dlg, &dr);
    std::printf("dialog client: %ld x %ld px\n", dr.right, dr.bottom);

    std::vector<HWND> kids;
    for (HWND w = GetWindow(dlg, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) kids.push_back(w);
    for (HWND w : kids) {
        const int id = GetDlgCtrlID(w);
        if (id >= IDC_PAGE_GENERAL && id <= IDC_PAGE_FONTS) {
            if (id != IDC_PAGE_GENERAL) std::reverse(kids.begin(), kids.end());
            break;
        }
    }

    HDC dc = GetDC(dlg);
    std::vector<Item> items;
    int page = -1;
    for (HWND w : kids) {
        Item it{};
        it.hwnd = w;
        it.id = GetDlgCtrlID(w);
        if (it.id == IDC_TABS) continue;
        if (it.id >= IDC_PAGE_GENERAL && it.id <= IDC_PAGE_FONTS) {
            page = it.id - IDC_PAGE_GENERAL;
            continue;
        }
        it.page = page;
        it.cls = className(w);
        it.text = windowText(w);
        GetWindowRect(w, &it.r);
        MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&it.r), 2);
        it.style = static_cast<DWORD>(GetWindowLongW(w, GWL_STYLE));
        items.push_back(it);
    }

    int problems = 0;
    int bottom[4] = {0, 0, 0, 0};
    for (const Item& it : items) {
        const int w = it.r.right - it.r.left;
        const int h = it.r.bottom - it.r.top;
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(it.hwnd, WM_GETFONT, 0, 0));
        const wchar_t* pg = it.page < 0 ? L"common" : kNames[it.page];
        auto report = [&](const char* what, const std::wstring& t) {
            std::printf("%-9s id=%d %s: \"%ls\" (box %dx%d at %ld,%ld)\n", (const char*)nullptr ? "" : "PROBLEM", it.id, what, t.c_str(), w, h,
                        it.r.left, it.r.top);
            (void)pg;
            ++problems;
        };
        if (it.r.right > dr.right || it.r.bottom > dr.bottom || it.r.left < 0 || it.r.top < 0) report("outside dialog", it.text);
        if (it.page >= 0) bottom[it.page] = (std::max)(bottom[it.page], static_cast<int>(it.r.bottom));

        const bool button = it.cls == L"Button";
        const DWORD type = it.style & BS_TYPEMASK;
        if (it.cls == L"Static" && !it.text.empty()) {
            if (textHeight(dc, font, it.text, w) > h + 1) report("static text does not fit", it.text);
        } else if (button && (type == BS_AUTOCHECKBOX || type == BS_CHECKBOX)) {
            const int avail = w - 18;
            if (textWidth(dc, font, it.text) > avail && textHeight(dc, font, it.text, avail) > h) report("checkbox text does not fit", it.text);
        } else if (button) {
            if (textWidth(dc, font, it.text) > w - 10) report("button text does not fit", it.text);
        } else if (it.cls == L"ComboBox") {
            int need = 0;
            for (const std::wstring& s : comboItems(it.id)) need = (std::max)(need, textWidth(dc, font, s));
            if (need > w - GetSystemMetrics(SM_CXVSCROLL) - 8) report("combo item does not fit", L"longest item");
        } else if (it.cls == L"Edit") {
            int need = 0;
            const bool hexBox = it.id == IDC_BG_HEX || it.id == IDC_TEXT_HEX || it.id == IDC_ACCENT_HEX;
            if (hexBox) need = textWidth(dc, font, L"FFFFFF");
            else if (it.style & ES_NUMBER) need = textWidth(dc, font, L"0000");
            if (need && need > w - 8) report("edit too narrow", L"value");
        }
    }

    // Overlaps: same page, or common vs any page. Etched rules (1 px high) are decoration.
    for (size_t i = 0; i < items.size(); ++i) {
        for (size_t j = i + 1; j < items.size(); ++j) {
            const Item& a = items[i];
            const Item& b = items[j];
            if (a.page >= 0 && b.page >= 0 && a.page != b.page) continue;
            if (a.r.bottom - a.r.top <= 3 || b.r.bottom - b.r.top <= 3) continue;
            RECT x{};
            if (IntersectRect(&x, &a.r, &b.r)) {
                std::printf("PROBLEM overlap: id=%d \"%ls\" and id=%d \"%ls\"\n", a.id, a.text.c_str(), b.id, b.text.c_str());
                ++problems;
            }
        }
    }

    for (int p = 0; p < 4; ++p) std::printf("page %-13ls content bottom = %d px of %ld\n", kNames[p], bottom[p], dr.bottom);
    std::printf("controls checked: %zu, problems: %d\n", items.size(), problems);
    ReleaseDC(dlg, dc);
    return problems ? 1 : 0;
}
