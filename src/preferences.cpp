// Preferences > Tools > On-screen display.
//
// The page holds a tab strip and a Preview row; each tab (General, Style, Elements, Text, Fonts)
// is its own child dialog (see foo_osd.rc), created once and shown one at a time. Control ids are
// unique across the tabs, so dialogItem() finds any control wherever it lives and the children
// forward their commands here. The dialog edits a Settings value; apply() saves it. "Preview"
// shows the card with the dialog's current, unsaved values. The Preset box names the preset the
// look matches, or "Custom" once it has been changed by hand.

#include <helpers/foobar2000+atl.h>
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <initializer_list>
#include <iterator>
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

// The Position box lists the spots in reading order; Settings::position keeps the stored order.
constexpr int kPositionOrder[] = {TopLeft,    TopCenter,    TopRight,    MiddleLeft, MiddleCenter,
                                  MiddleRight, BottomLeft, BottomCenter, BottomRight};
static_assert(std::size(kPositionOrder) == PositionCount);

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

//! What the Font dialog's family box said when OK was pressed. Only filled for family-only picks.
thread_local std::wstring g_picked_family;

//! For a fallback font only the family matters, so the dialog's style and size pickers are hidden
//! (they would suggest a choice that has no effect) and the caption says so.
UINT_PTR CALLBACK family_only_hook(HWND dlg, UINT msg, WPARAM wparam, LPARAM) {
    if (msg == WM_INITDIALOG) {
        // stc2/stc3 (labels) and cmb2/cmb3 (style, size) of the common Font dialog.
        for (int id : {0x441, 0x442, 0x471, 0x472})
            if (HWND h = GetDlgItem(dlg, id)) ShowWindow(h, SW_HIDE);
        SetWindowTextW(dlg, L"Fallback font (family only)");
        g_picked_family.clear();
    } else if (msg == WM_COMMAND && LOWORD(wparam) == IDOK) {
        // cmb1, the family box: the name the user actually chose.
        wchar_t name[LF_FACESIZE]{};
        GetDlgItemTextW(dlg, 0x470, name, LF_FACESIZE);
        g_picked_family = name;
    }
    return 0;
}

//! The family a picked font belongs to, weights and all. ChooseFont names the GDI family of the
//! style it has selected, and a family with more than four styles is split into one GDI family per
//! weight: "IBM Plex Sans JP Thin". With the style list hidden that is whichever style comes first,
//! so a fallback came back as Thin. DirectWrite groups the weights, so its name is the one to keep.
//! Falls back to the GDI name when DirectWrite or GDI does not know the grouped name.
static int CALLBACK family_found(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}
[[nodiscard]] bool gdi_has_family(const std::wstring& name) {
    LOGFONTW probe{};
    probe.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(probe.lfFaceName, name.c_str(), _TRUNCATE);
    bool found = false;
    if (HDC screen = GetDC(nullptr)) {
        EnumFontFamiliesExW(screen, &probe, &family_found, reinterpret_cast<LPARAM>(&found), 0);
        ReleaseDC(nullptr, screen);
    }
    return found;
}
[[nodiscard]] std::wstring base_family(const LOGFONTW& picked) {
    const std::wstring gdi_name = picked.lfFaceName;
    CComPtr<IDWriteFactory> factory;
    CComPtr<IDWriteGdiInterop> interop;
    CComPtr<IDWriteFont> font;
    CComPtr<IDWriteFontFamily> family;
    CComPtr<IDWriteLocalizedStrings> names;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&factory))) ||
        FAILED(factory->GetGdiInterop(&interop)) ||
        FAILED(interop->CreateFontFromLOGFONT(&picked, &font)) ||
        FAILED(font->GetFontFamily(&family)) || FAILED(family->GetFamilyNames(&names))) {
        return gdi_name;
    }
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(names->FindLocaleName(L"en-us", &index, &exists)) || !exists) index = 0;
    UINT32 length = 0;
    if (FAILED(names->GetStringLength(index, &length)) || length == 0 || length >= LF_FACESIZE) {
        return gdi_name;
    }
    std::wstring name(length + 1, L'\0');
    if (FAILED(names->GetString(index, name.data(), length + 1))) return gdi_name;
    name.resize(length);
    // Only a name GDI can open too.
    return gdi_has_family(name) ? name : gdi_name;
}

//! The family a family-only pick stands for: the family box's text, which is what the user
//! clicked, else the DirectWrite grouping of whatever style the hidden style list held.
[[nodiscard]] std::wstring fallback_family(const LOGFONTW& picked) {
    if (!g_picked_family.empty() && gdi_has_family(g_picked_family)) return g_picked_family;
    return base_family(picked);
}

//! Text fields get some room before the first character: the Edit control's own margin is a
//! pixel or two with the dialog font, so the text sat against the border. 4 px at 96 DPI.
void pad_text_fields(HWND dialog) {
    HDC dc = GetDC(dialog);
    const int dpi = dc != nullptr ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc != nullptr) ReleaseDC(dialog, dc);
    const int pad = MulDiv(4, dpi, 96);
    EnumChildWindows(
        dialog,
        [](HWND child, LPARAM pad) -> BOOL {
            wchar_t name[16]{};
            GetClassNameW(child, name, 16);
            if (_wcsicmp(name, L"Edit") == 0) {
                SendMessageW(child, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                             MAKELPARAM(pad, pad));
            }
            return TRUE;
        },
        pad);
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
        dialog.lpfnHook = &family_only_hook;
    }
    if (!ChooseFontW(&dialog) || logical.lfFaceName[0] == L'\0') return false;

    const pfc::stringcvt::string_utf8_from_wide name(
        familyOnly ? fallback_family(logical).c_str() : logical.lfFaceName);
    f.family.assign(name.get_ptr(), name.length());
    if (!familyOnly) {
        f.tenthsPt = dialog.iPointSize > 0 ? dialog.iPointSize : 0;
        f.weight = logical.lfWeight > 0 ? logical.lfWeight : FW_NORMAL;
        f.italic = logical.lfItalic != 0;
    }
    return true;
}

//! "Save preset": asks for a name. Save stays disabled while the name is empty or a built-in's.
class PresetNameDialog : public CDialogImpl<PresetNameDialog> {
public:
    enum { IDD = IDD_OSD_PRESET_NAME };

    explicit PresetNameDialog(std::string initial) : m_name(std::move(initial)) {}
    const std::string& name() const { return m_name; }

    BEGIN_MSG_MAP_EX(PresetNameDialog)
        MSG_WM_INITDIALOG(onInitDialog)
        COMMAND_HANDLER_EX(IDC_PRESET_NAME, EN_CHANGE, onNameChanged)
        COMMAND_ID_HANDLER_EX(IDOK, onOk)
        COMMAND_ID_HANDLER_EX(IDCANCEL, onCancel)
    END_MSG_MAP()

private:
    BOOL onInitDialog(CWindow, LPARAM) {
        m_dark.AddDialogWithControls(*this);
        CEdit edit(GetDlgItem(IDC_PRESET_NAME));
        edit.LimitText(64);
        edit.SetWindowTextW(pfc::stringcvt::string_wide_from_utf8(m_name.c_str()).get_ptr());
        edit.SetSelAll();
        edit.SetFocus();
        update();
        return FALSE; // focus set here
    }
    std::string typed() const {
        wchar_t buf[128] = {};
        ::GetDlgItemTextW(m_hWnd, IDC_PRESET_NAME, buf, static_cast<int>(std::size(buf)));
        return std::string(pfc::stringcvt::string_utf8_from_wide(buf).get_ptr());
    }
    void update() { GetDlgItem(IDOK).EnableWindow(presets::validUserName(typed())); }
    void onNameChanged(UINT, int, CWindow) { update(); }
    void onOk(UINT, int, CWindow) {
        m_name = typed();
        if (presets::validUserName(m_name)) EndDialog(IDOK);
    }
    void onCancel(UINT, int, CWindow) { EndDialog(IDCANCEL); }

    std::string m_name;
    fb2k::CDarkModeHooks m_dark;
};

class PreferencesInstance : public CDialogImpl<PreferencesInstance>, public preferences_page_instance {
public:
    explicit PreferencesInstance(preferences_page_callback::ptr callback) : m_callback(callback) {}

    enum { IDD = IDD_OSD_PREFERENCES };

    // preferences_page_instance
    t_uint32 get_state() override {
        t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (fromDialog().serialize() != Settings::current().serialize()) state |= preferences_state::changed;
        return state;
    }
    void apply() override {
        fromDialog().save();
        settingsChanged();
        toDialog(Settings::current()); // show the values as stored, clamped to their ranges
        m_callback->on_state_changed();
    }
    void reset() override {
        toDialog(Settings{});
        m_callback->on_state_changed();
    }

    BEGIN_MSG_MAP_EX(PreferencesInstance)
        MSG_WM_INITDIALOG(onInitDialog)
        MSG_WM_DRAWITEM(onDrawItem)
        MESSAGE_HANDLER_EX(WM_NOTIFY, onNotify)
        COMMAND_CODE_HANDLER_EX(EN_CHANGE, onControlChanged)
        COMMAND_CODE_HANDLER_EX(BN_CLICKED, onControlChanged)
        COMMAND_CODE_HANDLER_EX(CBN_SELCHANGE, onControlChanged)
    END_MSG_MAP()

private:
    // ---- setup ----------------------------------------------------------------------------

    BOOL onInitDialog(CWindow, LPARAM) {
        m_dark.AddDialogWithControls(*this);
        createTabs();
        setupTabs();

        fill(IDC_POSITION, {L"Top left", L"Top center", L"Top right", L"Middle left", L"Centre", L"Middle right",
                            L"Bottom left", L"Bottom center", L"Bottom right"});
        fill(IDC_MONITOR, {L"foobar2000's", L"Primary", L"Mouse pointer's"});
        fill(IDC_LAYOUT, {L"Classic", L"Compact", L"Banner", L"Poster"});
        fill(IDC_BG_MODE, {L"Dark", L"Light", L"Cover tint", L"Cover colour", L"Custom"});
        fill(IDC_TEXT_MODE, {L"Automatic", L"Custom"});
        fill(IDC_BORDER, {L"None", L"Subtle", L"Accent"});
        fill(IDC_ART_SHAPE, {L"Rounded", L"Circle", L"Square"});
        fill(IDC_ANIMATION, {L"Fade", L"Slide", L"Glide", L"None"});
        fill(IDC_BAR_STYLE, {L"Rounded", L"Thin", L"Thick"});

        fillPresets();

        // Title formatting fits comfortably in kTextMax; hex fields take "#RRGGBB".
        for (int id : {IDC_LINE1, IDC_LINE2, IDC_LINE3}) GetDlgItem(id).SendMessage(EM_LIMITTEXT, kTextMax - 1);
        for (int id : {IDC_BG_HEX, IDC_TEXT_HEX, IDC_ACCENT_HEX}) GetDlgItem(id).SendMessage(EM_LIMITTEXT, 7);
        for (int id : {IDC_SECONDS, IDC_MARGIN, IDC_SCALE, IDC_OPACITY, IDC_RADIUS, IDC_ANIM_SPEED})
            GetDlgItem(id).SendMessage(EM_LIMITTEXT, 3);

        toDialog(Settings::current());
        showPage(0);
        return FALSE;
    }

    void setupTabs() {
        const HWND tabs = ::GetDlgItem(m_hWnd, IDC_TABS);
        const wchar_t* const names[kTabCount] = {L"General", L"Style", L"Elements", L"Text", L"Fonts"};
        for (int i = 0; i < kTabCount; ++i) {
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
    }

    //! One child dialog per tab, placed over IDC_PAGE_HOST. The page's own dark-mode hooks only
    //! reach its direct children, so each tab gets its own AddDialogWithControls().
    void createTabs() {
        CRect host;
        ::GetWindowRect(::GetDlgItem(m_hWnd, IDC_PAGE_HOST), &host);
        ScreenToClient(&host);
        for (int i = 0; i < kTabCount; ++i) {
            HWND tab = ::CreateDialogParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_OSD_TAB_GENERAL + i), m_hWnd,
                                            &PreferencesInstance::tabDialogProc, 0);
            m_tabs[i] = tab;
            if (tab == nullptr) continue;
            ::SetWindowPos(tab, ::GetDlgItem(m_hWnd, IDC_PAGE_HOST), host.left, host.top, host.Width(), host.Height(), SWP_NOACTIVATE);
            m_dark.AddDialogWithControls(tab);
            pad_text_fields(tab);
        }
    }

    //! Tab dialogs keep nothing themselves: commands and owner-draw requests go to the page.
    static INT_PTR CALLBACK tabDialogProc(HWND tab, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_INITDIALOG: return FALSE;
        case WM_COMMAND:
        case WM_DRAWITEM: ::SendMessageW(::GetParent(tab), message, wparam, lparam); return TRUE;
        default: return FALSE;
        }
    }

    //! The tab strip's TCN_SELCHANGE. Raw WM_NOTIFY rather than NOTIFY_HANDLER_EX: this page has
    //! been sent WM_NOTIFY with an lParam that is no pointer at all (0, 0x4E), which crashes any
    //! handler that reads the header. Anything below 64 KB cannot be a valid address.
    LRESULT onNotify(UINT, WPARAM, LPARAM lparam) {
        SetMsgHandled(FALSE);
        if (lparam < 0x10000) return 0;
        const auto* header = reinterpret_cast<const NMHDR*>(lparam);
        if (header->idFrom == IDC_TABS && header->code == TCN_SELCHANGE) {
            showPage(TabCtrl_GetCurSel(::GetDlgItem(m_hWnd, IDC_TABS)));
            SetMsgHandled(TRUE);
        }
        return 0;
    }

    void showPage(int page) {
        m_page = page;
        for (int i = 0; i < kTabCount; ++i) {
            if (m_tabs[i] != nullptr) ::ShowWindow(m_tabs[i], i == page ? SW_SHOWNA : SW_HIDE);
        }
    }

    //! A control by id on whichever tab holds it (ids are unique across the tabs), or on the page.
    //! These shadow CWindow's own GetDlgItem family so the rest of the class reads as one dialog.
    CWindow GetDlgItem(int id) const {
        for (HWND tab : m_tabs) {
            if (tab == nullptr) continue;
            if (HWND w = ::GetDlgItem(tab, id)) return CWindow(w);
        }
        return CWindow(::GetDlgItem(m_hWnd, id));
    }
    HWND ownerOf(int id) const { return GetDlgItem(id).GetParent(); }
    UINT IsDlgButtonChecked(int id) const { return ::IsDlgButtonChecked(ownerOf(id), id); }
    BOOL CheckDlgButton(int id, UINT state) const { return ::CheckDlgButton(ownerOf(id), id, state); }
    UINT GetDlgItemInt(int id, BOOL* ok, BOOL isSigned) const { return ::GetDlgItemInt(ownerOf(id), id, ok, isSigned); }
    BOOL SetDlgItemInt(int id, UINT value, BOOL isSigned) const { return ::SetDlgItemInt(ownerOf(id), id, value, isSigned); }
    UINT GetDlgItemTextW(int id, LPWSTR buffer, int size) const { return ::GetDlgItemTextW(ownerOf(id), id, buffer, size); }
    BOOL SetDlgItemTextW(int id, LPCWSTR text) const { return ::SetDlgItemTextW(ownerOf(id), id, text); }

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
        if (const int hexId = hexForSwatch(id)) {
            pickColour(hexId);
            return;
        }
        if (id == IDC_BG_HEX || id == IDC_TEXT_HEX || id == IDC_ACCENT_HEX) GetDlgItem(swatchForHex(id)).Invalidate();
        if (id == IDC_TITLEFORMAT_HELP) {
            openTitleFormatHelp();
            return;
        }
        if (id == IDC_TEXT_DEFAULT) { // back to title / artist / album (year)
            const Settings d;
            setText(IDC_LINE1, d.line1);
            setText(IDC_LINE2, d.line2);
            setText(IDC_LINE3, d.line3);
            m_callback->on_state_changed();
            return;
        }
        if (id == IDC_PRESET) {
            onPreset();
            return;
        }
        if (id == IDC_PRESET_SAVE) {
            onSavePreset();
            return;
        }
        if (id == IDC_PRESET_DELETE) {
            onDeletePreset();
            return;
        }
        if (onFontButton(id)) {
            showFonts();
            m_callback->on_state_changed();
            return;
        }
        // A hand edit may leave the look of a preset (or bring it back).
        syncPreset();
        updateEnables();
        m_callback->on_state_changed();
    }

    void onPreset() {
        CComboBox box(GetDlgItem(IDC_PRESET));
        const int sel = box.GetCurSel();
        if (sel <= 0) {
            updatePresetButtons();
            return;
        }
        Settings s = fromDialog();
        presets::apply(sel - 1, s);
        toDialog(s);
        box.SetCurSel(sel);
        updatePresetButtons();
        showPreview(s);
        m_callback->on_state_changed();
    }

    //! "Custom", then every preset. Built-ins and the user's own share the list; a user preset is
    //! told apart only by Delete being enabled.
    void fillPresets() {
        CComboBox box(GetDlgItem(IDC_PRESET));
        box.ResetContent();
        box.AddString(L"Custom");
        for (int i = 0; i < presets::count(); ++i) box.AddString(pfc::stringcvt::string_wide_from_utf8(presets::name(i)).get_ptr());
    }

    int selectedPreset() { return CComboBox(GetDlgItem(IDC_PRESET)).GetCurSel() - 1; }

    void updatePresetButtons() { GetDlgItem(IDC_PRESET_DELETE).EnableWindow(presets::isUser(selectedPreset())); }

    //! Keeps the chosen preset while the look still is that preset (a user preset may equal a
    //! built-in), otherwise shows the first match or "Custom".
    void syncPreset() {
        const Settings s = fromDialog();
        if (!presets::matches(selectedPreset(), s)) CComboBox(GetDlgItem(IDC_PRESET)).SetCurSel(presets::match(s) + 1);
        updatePresetButtons();
    }

    //! Saved at once, not on Apply: a preset is not part of the settings being edited.
    void onSavePreset() {
        const int sel = selectedPreset();
        std::string initial;
        if (presets::isUser(sel)) {
            initial = presets::name(sel);
        } else {
            for (int n = 1; initial.empty() || presets::find(initial) >= 0; ++n) initial = "My preset " + std::to_string(n);
        }
        PresetNameDialog dialog(initial);
        if (dialog.DoModal(m_hWnd) != IDOK) return;
        const int index = presets::saveUser(dialog.name(), fromDialog());
        if (index < 0) return;
        fillPresets();
        CComboBox(GetDlgItem(IDC_PRESET)).SetCurSel(index + 1);
        updatePresetButtons();
    }

    void onDeletePreset() {
        const int sel = selectedPreset();
        if (!presets::isUser(sel)) return;
        const std::wstring question = L"Delete the preset \u201C" + std::wstring(pfc::stringcvt::string_wide_from_utf8(presets::name(sel)).get_ptr()) + L"\u201D?";
        if (::MessageBoxW(m_hWnd, question.c_str(), L"On-screen display", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
        presets::removeUser(sel);
        fillPresets();
        CComboBox(GetDlgItem(IDC_PRESET)).SetCurSel(presets::match(fromDialog()) + 1);
        updatePresetButtons();
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
        const bool bg = selection(IDC_BG_MODE) == BgCustom;
        const bool text = selection(IDC_TEXT_MODE) == TextCustom;
        GetDlgItem(IDC_BG_HEX).EnableWindow(bg);
        GetDlgItem(IDC_BG_SWATCH).EnableWindow(bg);
        GetDlgItem(IDC_TEXT_HEX).EnableWindow(text);
        GetDlgItem(IDC_TEXT_SWATCH).EnableWindow(text);
        // Options only enabled while the option they depend on is on.
        const bool on = checked(IDC_ENABLED);
        for (int id : {IDC_ON_TRACK, IDC_ON_STREAM, IDC_ON_PAUSE, IDC_ON_SEEK}) GetDlgItem(id).EnableWindow(on);
        const bool art = checked(IDC_SHOW_ART);
        GetDlgItem(IDC_ART_SHAPE).EnableWindow(art);
        GetDlgItem(IDC_WAIT_ART).EnableWindow(art);
        const bool bar = checked(IDC_SHOW_PROGRESS);
        for (int id : {IDC_SHOW_KNOB, IDC_SHOW_TIMES, IDC_BAR_STYLE}) GetDlgItem(id).EnableWindow(bar);
        GetDlgItem(IDC_SHOW_REMAINING).EnableWindow(bar && checked(IDC_SHOW_TIMES));
    }

    // ---- colour swatches ---------------------------------------------------------------------

    static int hexForSwatch(int id) {
        switch (id) {
        case IDC_BG_SWATCH: return IDC_BG_HEX;
        case IDC_TEXT_SWATCH: return IDC_TEXT_HEX;
        case IDC_ACCENT_SWATCH: return IDC_ACCENT_HEX;
        default: return 0;
        }
    }
    static int swatchForHex(int id) {
        switch (id) {
        case IDC_BG_HEX: return IDC_BG_SWATCH;
        case IDC_TEXT_HEX: return IDC_TEXT_SWATCH;
        default: return IDC_ACCENT_SWATCH;
        }
    }
    static COLORREF toColorref(std::uint32_t rgb) { return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }

    //! The Windows colour dialog, started on the field's colour; writes the pick back as hex,
    //! which fires EN_CHANGE and with it the usual change handling.
    void pickColour(int hexId) {
        static COLORREF custom[16] = {};
        CHOOSECOLORW dialog{};
        dialog.lStructSize = sizeof dialog;
        dialog.hwndOwner = m_hWnd;
        dialog.rgbResult = toColorref(hex(hexId, 0));
        dialog.lpCustColors = custom;
        dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (!ChooseColorW(&dialog)) return;
        const COLORREF c = dialog.rgbResult;
        setHex(hexId, (static_cast<std::uint32_t>(GetRValue(c)) << 16) | (GetGValue(c) << 8) | GetBValue(c));
    }

    void onDrawItem(int, LPDRAWITEMSTRUCT item) {
        const int hexId = hexForSwatch(static_cast<int>(item->CtlID));
        if (hexId == 0) return;
        CDCHandle dc(item->hDC);
        CRect rc(item->rcItem);
        HWND tab = ::GetParent(item->hwndItem);
        // The tab's own background brush, so the swatch sits right in light and dark mode.
        auto brush = reinterpret_cast<HBRUSH>(::SendMessageW(tab, WM_CTLCOLORDLG, reinterpret_cast<WPARAM>(item->hDC), reinterpret_cast<LPARAM>(tab)));
        dc.FillRect(rc, brush != nullptr ? brush : ::GetSysColorBrush(COLOR_BTNFACE));
        const bool dark = DarkMode::IsDialogDark(tab);
        const bool disabled = (item->itemState & ODS_DISABLED) != 0;
        CRect swatch = rc;
        swatch.DeflateRect(1, 1);
        COLORREF fill = toColorref(hex(hexId, 0));
        if (disabled) {  // washed out towards the background, like a disabled control
            const COLORREF back = dark ? RGB(32, 32, 32) : ::GetSysColor(COLOR_BTNFACE);
            fill = RGB((GetRValue(fill) + 2 * GetRValue(back)) / 3, (GetGValue(fill) + 2 * GetGValue(back)) / 3,
                       (GetBValue(fill) + 2 * GetBValue(back)) / 3);
        }
        dc.FillSolidRect(swatch, fill);
        CBrush frame;
        frame.CreateSolidBrush(dark ? RGB(130, 130, 130) : ::GetSysColor(COLOR_BTNSHADOW));
        dc.FrameRect(swatch, frame);
        if ((item->itemState & ODS_FOCUS) != 0 && (item->itemState & ODS_NOFOCUSRECT) == 0) dc.DrawFocusRect(rc);
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
        s.onStreamTitle = checked(IDC_ON_STREAM);
        s.onPause = checked(IDC_ON_PAUSE);
        s.onSeek = checked(IDC_ON_SEEK);
        s.onlyWhenUnfocused = checked(IDC_ONLY_UNFOCUSED);
        s.hideInFullscreen = checked(IDC_HIDE_FULLSCREEN);
        s.holdWhilePaused = checked(IDC_HOLD_PAUSED);
        s.waitForArt = checked(IDC_WAIT_ART);
        s.fadeOnHover = checked(IDC_FADE_HOVER);
        s.seconds = number(IDC_SECONDS, s.seconds);

        const int spot = selection(IDC_POSITION);
        if (spot >= 0 && spot < PositionCount) s.position = kPositionOrder[spot];
        s.monitor = selection(IDC_MONITOR, s.monitor);
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
        s.showRemaining = checked(IDC_SHOW_REMAINING);
        s.showGlyph = checked(IDC_SHOW_GLYPH);
        s.showKnob = checked(IDC_SHOW_KNOB);
        s.shadow = checked(IDC_SHADOW);
        s.sheen = checked(IDC_SHEEN);
        s.clearType = checked(IDC_CLEARTYPE);
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
        check(IDC_ON_STREAM, s.onStreamTitle);
        check(IDC_ON_PAUSE, s.onPause);
        check(IDC_ON_SEEK, s.onSeek);
        check(IDC_ONLY_UNFOCUSED, s.onlyWhenUnfocused);
        check(IDC_HIDE_FULLSCREEN, s.hideInFullscreen);
        check(IDC_HOLD_PAUSED, s.holdWhilePaused);
        check(IDC_WAIT_ART, s.waitForArt);
        check(IDC_FADE_HOVER, s.fadeOnHover);
        SetDlgItemInt(IDC_SECONDS, static_cast<UINT>(s.seconds), FALSE);

        const int* spot = std::find(std::begin(kPositionOrder), std::end(kPositionOrder), s.position);
        CComboBox(GetDlgItem(IDC_POSITION)).SetCurSel(spot != std::end(kPositionOrder) ? static_cast<int>(spot - kPositionOrder) : 2);
        CComboBox(GetDlgItem(IDC_MONITOR)).SetCurSel(s.monitor);
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
        check(IDC_SHOW_REMAINING, s.showRemaining);
        check(IDC_SHOW_GLYPH, s.showGlyph);
        check(IDC_SHOW_KNOB, s.showKnob);
        check(IDC_SHADOW, s.shadow);
        check(IDC_SHEEN, s.sheen);
        check(IDC_CLEARTYPE, s.clearType);
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

        m_loading = false;
        syncPreset();
        updateEnables();
    }

    const preferences_page_callback::ptr m_callback;
    fb2k::CDarkModeHooks m_dark; // must be a member of the dialog class
    bool m_loading = false;
    int m_page = -1;
    static constexpr int kTabCount = 5;
    HWND m_tabs[kTabCount] = {};
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
