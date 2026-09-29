// The user's UI font, for the "Default" font choice. Columns UI first (its common labels font,
// which is what panels such as this follow), then Default UI's font. No fonts are cached: a
// lookup is a service call and a GetObject, and it runs once per card, so a font changed in
// Preferences applies to the next card with no notification plumbing.

#include <helpers/foobar2000+atl.h>

#include <columns_ui-sdk/ui_extension.h>

#include "host_font.h"

namespace osd {
namespace {

std::string toUtf8(const wchar_t* face) {
    return std::string(pfc::stringcvt::string_utf8_from_wide(face).get_ptr());
}

} // namespace

std::string hostFontFamily() {
    try {
        if (const auto lf = cui::fonts::get_log_font(cui::fonts::labels_font_id)) {
            if (lf->lfFaceName[0] != L'\0') return toUtf8(lf->lfFaceName);
        }
    } catch (...) {
    }
    try {
        if (ui_config_manager::ptr cfg; fb2k::std_api_try_get(cfg)) {
            if (HFONT font = cfg->query_font(ui_font_default)) {
                LOGFONT lf{};
                if (GetObject(font, sizeof(lf), &lf) > 0 && lf.lfFaceName[0] != L'\0') return toUtf8(lf.lfFaceName);
            }
        }
    } catch (...) {
    }
    return std::string();
}

void applyHostFonts(Settings& s) {
    if (!s.titleFont.empty() && !s.detailFont.empty()) return;
    const std::string host = hostFontFamily();
    if (host.empty()) return;
    if (s.titleFont.empty()) s.titleFont = host;
    if (s.detailFont.empty()) s.detailFont = host;
}

} // namespace osd
