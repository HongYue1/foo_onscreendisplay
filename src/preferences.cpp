// Preferences > Tools > On-screen display.
//
// Four pages (General, Appearance, Text, Fonts) in one dialog template; the tab strip only picks
// which run of controls is visible (see foo_osd.rc). The dialog edits a Settings value; apply()
// saves it. "Preview" shows the card with the dialog's current, unsaved values.

#include <helpers/foobar2000+atl.h>
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <initializer_list>
#include <string>
#include <vector>

#include "../resource.h"
#include "config.h"
#include "controller.h"
#include "guids.h"
#include "host_font.h"
#include "presets.h"

namespace osd {
namespace {

constexpr int kTextMax = 1024;
constexpr int kFallbackCount = 3;

// One font as the Windows font dialog reports it.
struct FontSel {
    std::string family; // empty = default
    int tenthsPt = 0;   // 0 = the layout's own size
    int weight = 0;     // 0 = default (bold title, regular detail)
    bool italic = false;
};

//! "IBM Plex Sans, 12pt, Bold, Italic". Nothing picked reads as "Default (<UI font>)".
std::string describeFont(const FontSel& f, const std::string& host, int defaultTenths, const char* defaultStyle) {
    const std::string family = f.family.empty() ? (host.empty() ? std::string("Segoe UI") : host) : f.family;
    if (f.family.empty() && (f.tenthsPt == 0 || f.tenthsPt == defaultTenths) && f.weight == 0 && !f.italic) {
        std::string text = "Default (" + family;
        char size[24]{};
        if (defaultTenths % 10 == 0) std::snprintf(size, sizeof size, ", %dpt", defaultTenths / 10);
        else std::snprintf(size, sizeof size, ", %d.%dpt", defaultTenths / 10, defaultTenths % 10);
        text += size;
        if (*defaultStyle) (text += ", ") += defaultStyle;
        return text + ")";
    }
    std::string text = family;
    if (f.tenthsPt != 0) {
        char size[24]{};
        if (f.tenthsPt % 10 == 0) std::snprintf(size, sizeof size, ", %dpt", f.tenthsPt / 10);
        else std::snprintf(size, sizeof size, ", %d.%dpt", f.tenthsPt / 10, f.tenthsPt % 10);
        text += size;
    }
    const char* weight = nullptr;
    switch (f.weight / 100) {
    case 1: weight = "Thin"; break;
    case 2: weight = "Extra Light"; break;
    case 3: weight = "Light"; break;
    case 4: weight = "Regular"; break;
    case 5: weight = "Medium"; break;
    case 6: weight = "Semi Bold"; break;
    case 7: weight = "Bold"; break;
    case 8: weight = "Extra Bold"; break;
    case 9: weight = "Black"; break;
    default: break;
    }
    if (weight) (text += ", ") += weight;
    if (f.italic) text += ", Italic";
    return text;
}

//! For a fallback font only the family matters, so the dialog's style and size pickers are hidden
//! (they would suggest a choice that has no effect) and the caption says so.
UINT_PTR CALLBACK familyOnlyHook(HWND dlg, UINT msg, WPARAM, LPARAM) {
    if (msg == WM_INITDIALOG) {
        // stc2/stc3 (labels) and cmb2/cmb3 (style, size) of the common Font dialog.
        for (int id : {0x441, 0x442, 0x471, 0x472})
            if (HWND h = GetDlgItem(dlg, id)) ShowWindow(h, SW_HIDE);
        SetWindowTextW(dlg, L"Fallback font (family only)");
    }
    return 0;
}

//! The Windows font dialog, started from what is already chosen. Returns false on Cancel.
//! defaultTenths / defaultWeight are what an unset choice actually renders as.
bool pickFont(HWND owner, FontSel& f, int defaultTenths, int defaultWeight, bool familyOnly) {
    HDC screen = GetDC(nullptr);
    const int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    if (screen) ReleaseDC(nullptr, screen);

    LOGFONTW logical{};
    logical.lfCharSet = DEFAULT_CHARSET;
    logical.lfWeight = f.weight != 0 ? f.weight : defaultWeight;
    logical.lfItalic = f.italic ? TRUE : FALSE;
    logical.lfHeight = -MulDiv(f.tenthsPt != 0 ? f.tenthsPt : defaultTenths, dpi, 720);
    // Unset means the user's UI font, so that is what the dialog should start on.
    std::string family = f.family;
    if (family.empty()) family = hostFontFamily();
    if (family.empty()) family = "Segoe UI";
    const pfc::stringcvt::string_wide_from_utf8 face(family.c_str());
    wcsncpy_s(logical.lfFaceName, face.get_ptr(), _TRUNCATE);

    CHOOSEFONTW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = owner;
    dialog.lpLogFont = &logical;
    dialog.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
    // A fallback only lends its family: size and style come from the line's own font.
    if (familyOnly) {
        dialog.Flags |= CF_NOSIZESEL | CF_NOSTYLESEL | CF_ENABLEHOOK;
        dialog.lpfnHook = &familyOnlyHook;
    }
    if (!ChooseFontW(&dialog) || logical.lfFaceName[0] == L'\0') return false;

    const pfc::stringcvt::string_utf8_from_wide name(logical.lfFaceName);
    f.family.assign(name.get_ptr(), name.length());
    if (!familyOnly) {
        f.tenthsPt = dialog.iPointSize > 0 ? dialog.iPointSize : 0;
        f.weight = logical.lfWeight > 0 ? logical.lfWeight : FW_NORMAL;
        f.italic = logical.lfItalic != 0;
    }
    return true;
}

class PreferencesInstance : public CDialogImpl<PreferencesInstance>, public preferences_page_instance {
public:
    explicit PreferencesInstance(preferences_page_callback::ptr callback) : m_callback(callback) {}

    enum { IDD = IDD_OSD_PREFERENCES };

    // preferences_page_instance
    t_uint32 get_state() override {
        t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (fromDialog().serialize() != Settings::load().serialize()) state |= preferences_state::changed;
        return state;
    }
    void apply() override {
        fromDialog().save();
        m_callback->on_state_changed();
    }
    void reset() override {
        toDialog(Settings{});
        m_callback->on_state_changed();
    }

    BEGIN_MSG_MAP_EX(PreferencesInstance)
        MSG_WM_INITDIALOG(onInitDialog)
        COMMAND_CODE_HANDLER_EX(EN_CHANGE, onControlChanged)
        COMMAND_CODE_HANDLER_EX(BN_CLICKED, onControlChanged)
        COMMAND_CODE_HANDLER_EX(CBN_SELCHANGE, onControlChanged)
    END_MSG_MAP()

private:
    // ---- setup ----------------------------------------------------------------------------

    BOOL onInitDialog(CWindow, LPARAM) {
        m_dark.AddDialogWithControls(*this);
        setupTabs();

        fill(IDC_POSITION, {L"Top left", L"Top center", L"Top right", L"Bottom left", L"Bottom center", L"Bottom right"});
        fill(IDC_LAYOUT, {L"Classic", L"Compact", L"Banner", L"Poster"});
        fill(IDC_BG_MODE, {L"Dark", L"Light", L"Cover tint", L"Cover colour", L"Custom"});
        fill(IDC_TEXT_MODE, {L"Automatic", L"Custom"});
        fill(IDC_BORDER, {L"None", L"Subtle", L"Accent"});
        fill(IDC_ART_SHAPE, {L"Rounded", L"Circle", L"Square"});
        fill(IDC_ANIMATION, {L"Fade", L"Slide", L"Glide", L"None"});
        fill(IDC_BAR_STYLE, {L"Rounded", L"Thin", L"Thick"});

        CComboBox preset(GetDlgItem(IDC_PRESET));
        preset.AddString(L"Choose a preset...");
        for (int i = 0; i < presets::count(); ++i) {
            preset.AddString(pfc::stringcvt::string_wide_from_utf8(presets::name(i)).get_ptr());
        }

        toDialog(Settings::load());
        showPage(0);
        return FALSE;
    }

    void setupTabs() {
        // The strip must sit under the page controls in z-order, whichever way round the dialog
        // manager stacked the template.
        const HWND tabs = ::GetDlgItem(m_hWnd, IDC_TABS);
        ::SetWindowPos(tabs, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        const wchar_t* const names[] = {L"General", L"Appearance", L"Text", L"Fonts"};
        for (int i = 0; i < 4; ++i) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(names[i]);
            ::SendMessageW(tabs, TCM_INSERTITEMW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&item));
        }
        // Only the strip is wanted: the control would otherwise paint an empty page frame under
        // it, which reads as a stray rectangle.
        RECT item{};
        RECT client{};
        ::SendMessageW(tabs, TCM_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&item));
        ::GetClientRect(tabs, &client);
        ::SetWindowPos(tabs, nullptr, 0, 0, client.right, item.bottom + 2, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        // The strip's notifications go to its parent with an unreliable lParam here, so watch the
        // strip itself instead.
        ::SetWindowSubclass(tabs, &PreferencesInstance::tabProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }

    static LRESULT CALLBACK tabProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR self) {
        if (message == WM_NCDESTROY) {
            ::RemoveWindowSubclass(window, &PreferencesInstance::tabProc, id);
            return ::DefSubclassProc(window, message, wparam, lparam);
        }
        const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
        // A click or a key is the only way the selection moves, and it has moved by the time the
        // strip's own handling returns.
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_KEYDOWN || message == WM_KEYUP) {
            auto* dialog = reinterpret_cast<PreferencesInstance*>(self);
            const int now = static_cast<int>(::SendMessageW(window, TCM_GETCURSEL, 0, 0));
            if (now >= 0 && now != dialog->m_page) dialog->showPage(now);
        }
        return result;
    }

    void showPage(int page) {
        m_page = page;
        const auto marker = [](HWND window) {
            const int id = ::GetDlgCtrlID(window);
            return id >= IDC_PAGE_GENERAL && id <= IDC_PAGE_FONTS ? id - IDC_PAGE_GENERAL : -1;
        };

        std::vector<HWND> children;
        for (HWND w = ::GetWindow(m_hWnd, GW_CHILD); w != nullptr; w = ::GetWindow(w, GW_HWNDNEXT)) children.push_back(w);
        // Template order or its reverse: the first marker met says which.
        for (HWND w : children) {
            const int m = marker(w);
            if (m < 0) continue;
            if (m != 0) std::reverse(children.begin(), children.end());
            break;
        }

        int current = -1; // before the first marker: common to every page
        for (HWND w : children) {
            if (::GetDlgCtrlID(w) == IDC_TABS) continue;
            if (const int m = marker(w); m >= 0) {
                current = m;
                continue;
            }
            ::ShowWindow(w, (current < 0 || current == page) ? SW_SHOWNA : SW_HIDE);
        }
    }

    void fill(int id, std::initializer_list<const wchar_t*> items) {
        CComboBox box(GetDlgItem(id));
        for (const wchar_t* item : items) box.AddString(item);
    }

    // ---- events ---------------------------------------------------------------------------

    void onControlChanged(UINT, int id, CWindow) {
        if (m_loading) return;
        if (id == IDC_PREVIEW) {
            showPreview(fromDialog());
            return;
        }
        if (id == IDC_TITLEFORMAT_HELP) {
            openTitleFormatHelp();
            return;
        }
        if (id == IDC_TEXT_DEFAULT) { // back to title / artist / album (year)
            const Settings d;
            setText(IDC_LINE1, d.line1);
            setText(IDC_LINE2, d.line2);
            setText(IDC_LINE3, d.line3);
            CComboBox(GetDlgItem(IDC_PRESET)).SetCurSel(0);
            m_callback->on_state_changed();
            return;
        }
        if (id == IDC_PRESET) {
            onPreset();
            return;
        }
        if (onFontButton(id)) {
            CComboBox(GetDlgItem(IDC_PRESET)).SetCurSel(0);
            showFonts();
            m_callback->on_state_changed();
            return;
        }
        // Any hand edit means the look is no longer exactly the preset named in the box.
        CComboBox(GetDlgItem(IDC_PRESET)).SetCurSel(0);
        updateEnables();
        m_callback->on_state_changed();
    }

    void onPreset() {
        CComboBox box(GetDlgItem(IDC_PRESET));
        const int sel = box.GetCurSel();
        if (sel <= 0) return;
        Settings s = fromDialog();
        presets::apply(sel - 1, s);
        toDialog(s);
        box.SetCurSel(sel);
        showPreview(s);
        m_callback->on_state_changed();
    }

    // Returns true when id was a font Select/Default/Clear button (whether or not it changed anything).
    bool onFontButton(int id) {
        switch (id) {
        case IDC_TITLE_FONT_PICK: pickFont(m_hWnd, m_title, Settings::kTitleDefaultTenthsPt, FW_BOLD, false); return true;
        case IDC_DETAIL_FONT_PICK: pickFont(m_hWnd, m_detail, Settings::kDetailDefaultTenthsPt, FW_NORMAL, false); return true;
        case IDC_TITLE_FONT_CLEAR: m_title = FontSel{std::string(), Settings::kTitleDefaultTenthsPt, 0, false}; return true;
        case IDC_DETAIL_FONT_CLEAR: m_detail = FontSel{std::string(), Settings::kDetailDefaultTenthsPt, 0, false}; return true;
        default: break;
        }
        if (id >= IDC_FALLBACK_PICK && id < IDC_FALLBACK_PICK + kFallbackCount) {
            FontSel pick;
            std::string& slot = m_fallback[id - IDC_FALLBACK_PICK];
            pick.family = slot;
            if (pickFont(m_hWnd, pick, Settings::kDetailDefaultTenthsPt, FW_NORMAL, true)) slot = pick.family;
            return true;
        }
        if (id >= IDC_FALLBACK_CLEAR && id < IDC_FALLBACK_CLEAR + kFallbackCount) {
            m_fallback[id - IDC_FALLBACK_CLEAR].clear();
            return true;
        }
        return false;
    }

    void showFonts() {
        const std::string host = hostFontFamily();
        setText(IDC_TITLE_FONT, describeFont(m_title, host, Settings::kTitleDefaultTenthsPt, "Bold"));
        setText(IDC_DETAIL_FONT, describeFont(m_detail, host, Settings::kDetailDefaultTenthsPt, ""));
        for (int i = 0; i < kFallbackCount; ++i) setText(IDC_FALLBACK_TEXT + i, m_fallback[i].empty() ? std::string("None") : m_fallback[i]);
    }

    // titleformat_help.html ships with foobar2000 itself, in doc\ next to the executable. Derive
    // the path from the running process rather than guessing Program Files.
    void openTitleFormatHelp() {
        wchar_t module[MAX_PATH]{};
        const DWORD length = ::GetModuleFileNameW(nullptr, module, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return;
        std::wstring path(module, length);
        const size_t slash = path.find_last_of(L'\\');
        if (slash == std::wstring::npos) return;
        path.erase(slash + 1);
        path += L"doc\\titleformat_help.html";
        ::ShellExecuteW(m_hWnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void updateEnables() {
        GetDlgItem(IDC_BG_HEX).EnableWindow(selection(IDC_BG_MODE) == BgCustom);
        GetDlgItem(IDC_TEXT_HEX).EnableWindow(selection(IDC_TEXT_MODE) == TextCustom);
    }

    // ---- control <-> value helpers ----------------------------------------------------------

    bool checked(int id) { return IsDlgButtonChecked(id) == BST_CHECKED; }
    void check(int id, bool on) { CheckDlgButton(id, on ? BST_CHECKED : BST_UNCHECKED); }

    int number(int id, int fallback) {
        BOOL ok = FALSE;
        const UINT v = GetDlgItemInt(id, &ok, FALSE);
        return ok ? static_cast<int>(v) : fallback;
    }

    int selection(int id) { return CComboBox(GetDlgItem(id)).GetCurSel(); }
    int selection(int id, int fallback) {
        const int sel = selection(id);
        return sel >= 0 ? sel : fallback;
    }

    std::uint32_t hex(int id, std::uint32_t fallback) {
        wchar_t buf[32] = {};
        GetDlgItemTextW(id, buf, static_cast<int>(std::size(buf)));
        const wchar_t* p = buf;
        if (*p == L'#') ++p;
        if (*p == 0) return fallback;
        return static_cast<std::uint32_t>(wcstoul(p, nullptr, 16)) & 0xFFFFFFu;
    }

    void setHex(int id, std::uint32_t rgb) {
        wchar_t buf[16];
        swprintf_s(buf, L"%06X", static_cast<unsigned>(rgb & 0xFFFFFFu));
        SetDlgItemTextW(id, buf);
    }

    std::string text(int id) {
        wchar_t buf[kTextMax] = {};
        GetDlgItemTextW(id, buf, kTextMax);
        return std::string(pfc::stringcvt::string_utf8_from_wide(buf).get_ptr());
    }

    void setText(int id, const std::string& utf8) {
        SetDlgItemTextW(id, pfc::stringcvt::string_wide_from_utf8(utf8.c_str()).get_ptr());
    }

    // ---- Settings <-> dialog ----------------------------------------------------------------

    Settings fromDialog() {
        Settings s;
        s.enabled = checked(IDC_ENABLED);
        s.onTrack = checked(IDC_ON_TRACK);
        s.onPause = checked(IDC_ON_PAUSE);
        s.onSeek = checked(IDC_ON_SEEK);
        s.onlyWhenUnfocused = checked(IDC_ONLY_UNFOCUSED);
        s.hideInFullscreen = checked(IDC_HIDE_FULLSCREEN);
        s.followMonitor = checked(IDC_FOLLOW_MONITOR);
        s.seconds = number(IDC_SECONDS, s.seconds);

        s.position = selection(IDC_POSITION, s.position);
        s.margin = number(IDC_MARGIN, s.margin);
        s.scale = number(IDC_SCALE, s.scale);
        s.opacity = number(IDC_OPACITY, s.opacity);

        s.layout = selection(IDC_LAYOUT, s.layout);
        s.bgMode = selection(IDC_BG_MODE, s.bgMode);
        s.bgColor = hex(IDC_BG_HEX, s.bgColor);
        s.borderMode = selection(IDC_BORDER, s.borderMode);
        s.cornerRadius = number(IDC_RADIUS, s.cornerRadius);
        s.artShape = selection(IDC_ART_SHAPE, s.artShape);
        s.textMode = selection(IDC_TEXT_MODE, s.textMode);
        s.textColor = hex(IDC_TEXT_HEX, s.textColor);
        s.accent = hex(IDC_ACCENT_HEX, s.accent);
        s.animation = selection(IDC_ANIMATION, s.animation);
        s.animSpeed = number(IDC_ANIM_SPEED, s.animSpeed);
        s.barStyle = selection(IDC_BAR_STYLE, s.barStyle);

        s.showArt = checked(IDC_SHOW_ART);
        s.showProgress = checked(IDC_SHOW_PROGRESS);
        s.showTimes = checked(IDC_SHOW_TIMES);
        s.showGlyph = checked(IDC_SHOW_GLYPH);
        s.showKnob = checked(IDC_SHOW_KNOB);
        s.shadow = checked(IDC_SHADOW);
        s.sheen = checked(IDC_SHEEN);
        s.accentFromCover = checked(IDC_ACCENT_COVER);

        s.line1 = text(IDC_LINE1);
        s.line2 = text(IDC_LINE2);
        s.line3 = text(IDC_LINE3);
        s.titleFont = m_title.family;
        s.titlePt = m_title.tenthsPt;
        s.titleWeight = m_title.weight;
        s.titleItalic = m_title.italic;
        s.detailFont = m_detail.family;
        s.detailPt = m_detail.tenthsPt;
        s.detailWeight = m_detail.weight;
        s.detailItalic = m_detail.italic;
        s.fallback1 = m_fallback[0];
        s.fallback2 = m_fallback[1];
        s.fallback3 = m_fallback[2];
        s.clamp();
        return s;
    }

    void toDialog(const Settings& s) {
        m_loading = true;
        check(IDC_ENABLED, s.enabled);
        check(IDC_ON_TRACK, s.onTrack);
        check(IDC_ON_PAUSE, s.onPause);
        check(IDC_ON_SEEK, s.onSeek);
        check(IDC_ONLY_UNFOCUSED, s.onlyWhenUnfocused);
        check(IDC_HIDE_FULLSCREEN, s.hideInFullscreen);
        check(IDC_FOLLOW_MONITOR, s.followMonitor);
        SetDlgItemInt(IDC_SECONDS, static_cast<UINT>(s.seconds), FALSE);

        CComboBox(GetDlgItem(IDC_POSITION)).SetCurSel(s.position);
        SetDlgItemInt(IDC_MARGIN, static_cast<UINT>(s.margin), FALSE);
        SetDlgItemInt(IDC_SCALE, static_cast<UINT>(s.scale), FALSE);
        SetDlgItemInt(IDC_OPACITY, static_cast<UINT>(s.opacity), FALSE);

        CComboBox(GetDlgItem(IDC_LAYOUT)).SetCurSel(s.layout);
        CComboBox(GetDlgItem(IDC_BG_MODE)).SetCurSel(s.bgMode);
        setHex(IDC_BG_HEX, s.bgColor);
        CComboBox(GetDlgItem(IDC_BORDER)).SetCurSel(s.borderMode);
        SetDlgItemInt(IDC_RADIUS, static_cast<UINT>(s.cornerRadius), FALSE);
        CComboBox(GetDlgItem(IDC_ART_SHAPE)).SetCurSel(s.artShape);
        CComboBox(GetDlgItem(IDC_TEXT_MODE)).SetCurSel(s.textMode);
        setHex(IDC_TEXT_HEX, s.textColor);
        setHex(IDC_ACCENT_HEX, s.accent);
        CComboBox(GetDlgItem(IDC_ANIMATION)).SetCurSel(s.animation);
        SetDlgItemInt(IDC_ANIM_SPEED, static_cast<UINT>(s.animSpeed), FALSE);
        CComboBox(GetDlgItem(IDC_BAR_STYLE)).SetCurSel(s.barStyle);

        check(IDC_SHOW_ART, s.showArt);
        check(IDC_SHOW_PROGRESS, s.showProgress);
        check(IDC_SHOW_TIMES, s.showTimes);
        check(IDC_SHOW_GLYPH, s.showGlyph);
        check(IDC_SHOW_KNOB, s.showKnob);
        check(IDC_SHADOW, s.shadow);
        check(IDC_SHEEN, s.sheen);
        check(IDC_ACCENT_COVER, s.accentFromCover);

        setText(IDC_LINE1, s.line1);
        setText(IDC_LINE2, s.line2);
        setText(IDC_LINE3, s.line3);
        m_title = FontSel{s.titleFont, s.titlePt, s.titleWeight, s.titleItalic};
        m_detail = FontSel{s.detailFont, s.detailPt, s.detailWeight, s.detailItalic};
        m_fallback[0] = s.fallback1;
        m_fallback[1] = s.fallback2;
        m_fallback[2] = s.fallback3;
        showFonts();

        CComboBox(GetDlgItem(IDC_PRESET)).SetCurSel(0);
        m_loading = false;
        updateEnables();
    }

    const preferences_page_callback::ptr m_callback;
    fb2k::CDarkModeHooks m_dark; // must be a member of the dialog class
    bool m_loading = false;
    int m_page = -1;
    FontSel m_title;
    FontSel m_detail;
    std::string m_fallback[kFallbackCount];
};

class PreferencesPage : public preferences_page_impl<PreferencesInstance> {
public:
    const char* get_name() override { return "On-screen display"; }
    GUID get_guid() override { return guids::prefs_page; }
    GUID get_parent_guid() override { return preferences_page::guid_tools; }
};

preferences_page_factory_t<PreferencesPage> g_preferencesFactory;

} // namespace
} // namespace osd
