#include "presets.h"

namespace osd::presets {
namespace {

struct Preset {
    const char* name;
    void (*fn)(Settings&);
};

// The look every preset except Midnight was designed against (the original default look):
// Classic layout, rounded art, dark card with a soft shadow, gloss and a rounded bar.
void legacyBase(Settings& s) {
    s.opacity = 92;
    s.layout = LayoutClassic;
    s.artShape = ArtRounded;
    s.cornerRadius = 20;
    s.bgMode = BgDark;
    s.bgColor = 0x15151A;
    s.borderMode = BorderSubtle;
    s.barStyle = BarRounded;
    s.shadow = true;
    s.sheen = true;
}

void custom(Settings& s, std::uint32_t bg, std::uint32_t accent) {
    s.bgMode = BgCustom;
    s.bgColor = bg;
    s.accentFromCover = false;
    s.accent = accent;
}

const Preset kPresets[] = {
    {"Midnight (default)", [](Settings&) {}},
    {"Glass", [](Settings& s) {
         // Frosted: a see-through dark pane with a soft gloss and a fine edge.
         s.opacity = 62;
         s.cornerRadius = 24;
         s.borderMode = BorderSubtle;
         s.animation = AnimFade;
     }},
    {"Snow", [](Settings& s) {
         s.bgMode = BgLight;
         s.opacity = 95;
     }},
    {"Cover colour", [](Settings& s) {
         // The card takes the colour of the cover; text and bar stay neutral on top of it.
         s.bgMode = BgCoverColour;
         s.opacity = 100;
         s.cornerRadius = 18;
         s.borderMode = BorderNone;
         s.animation = AnimGlide;
     }},
    {"Cover tint", [](Settings& s) {
         // A dark card washed with the cover's colour; the bar takes the same colour.
         s.bgMode = BgCoverTint;
         s.opacity = 94;
         s.cornerRadius = 16;
         s.sheen = false;
         s.barStyle = BarThin;
     }},
    {"AMOLED", [](Settings& s) {
         s.bgMode = BgCustom;
         s.bgColor = 0x000000;
         s.opacity = 100;
         s.borderMode = BorderNone;
         s.shadow = false;
         s.sheen = false;
         s.cornerRadius = 24;
     }},
    {"Neon", [](Settings& s) {
         s.bgMode = BgCustom;
         s.bgColor = 0x090912;
         s.opacity = 96;
         s.borderMode = BorderAccent;
         s.sheen = false;
         s.cornerRadius = 14;
         s.barStyle = BarThick;
     }},
    {"Compact", [](Settings& s) {
         // Small and quiet: one tight row of art and text with a hairline bar.
         s.layout = LayoutCompact;
         s.cornerRadius = 14;
         s.opacity = 94;
         s.sheen = false;
         s.barStyle = BarThin;
         s.showKnob = false;
     }},
    {"Banner", [](Settings& s) {
         // Wide and low, for the top or bottom edge of the screen.
         s.layout = LayoutBanner;
         s.cornerRadius = 12;
         s.opacity = 94;
         s.sheen = false;
         s.barStyle = BarThin;
     }},
    {"Poster", [](Settings& s) {
         s.layout = LayoutPoster;
         s.cornerRadius = 22;
     }},
    {"Circle", [](Settings& s) {
         // Round cover like a record label, in a softly rounded card.
         s.layout = LayoutCompact;
         s.artShape = ArtCircle;
         s.cornerRadius = 30;
         s.opacity = 94;
     }},
    {"Pill", [](Settings& s) {
         // A capsule: round cover and text only, no progress bar.
         s.layout = LayoutCompact;
         s.artShape = ArtCircle;
         s.cornerRadius = 48;
         s.opacity = 94;
         s.sheen = false;
         s.showProgress = false;
     }},
    {"Flat", [](Settings& s) {
         s.cornerRadius = 0;
         s.shadow = false;
         s.sheen = false;
         s.borderMode = BorderNone;
         s.artShape = ArtSquare;
         s.animation = AnimFade;
     }},
    {"Retro terminal", [](Settings& s) {
         custom(s, 0x050A05, 0x33FF66);
         s.textMode = TextCustom;
         s.textColor = 0x33FF66;
         s.cornerRadius = 2;
         s.opacity = 96;
         s.borderMode = BorderAccent;
         s.shadow = false;
         s.sheen = false;
         s.showArt = false;
         s.barStyle = BarThin;
         s.showKnob = false;
         s.animation = AnimFade;
     }},
    {"Sunset", [](Settings& s) {
         custom(s, 0x2A1220, 0xFF7A59);
         s.textMode = TextCustom;
         s.textColor = 0xFFE6D9;
         s.borderMode = BorderAccent;
         s.cornerRadius = 24;
         s.animation = AnimGlide;
     }},
    {"Ocean", [](Settings& s) {
         custom(s, 0x0B1D2E, 0x3CC6FF);
         s.textMode = TextCustom;
         s.textColor = 0xE4F5FF;
         s.opacity = 90;
         s.cornerRadius = 16;
         s.borderMode = BorderSubtle;
         s.sheen = false;
         s.barStyle = BarThick;
     }},
    {"Forest", [](Settings& s) {
         custom(s, 0x0F1F16, 0x7BD88F);
         s.textMode = TextCustom;
         s.textColor = 0xE6F4E9;
         s.cornerRadius = 12;
         s.artShape = ArtSquare;
         s.borderMode = BorderSubtle;
         s.sheen = false;
         s.barStyle = BarThin;
         s.animation = AnimFade;
     }},
    {"Rose", [](Settings& s) {
         custom(s, 0x2A1522, 0xFF6FA5);
         s.textMode = TextCustom;
         s.textColor = 0xFFE6EF;
         s.layout = LayoutCompact;
         s.artShape = ArtCircle;
         s.borderMode = BorderAccent;
         s.cornerRadius = 28;
     }},
    {"Paper", [](Settings& s) {
         s.bgMode = BgLight;
         s.opacity = 98;
         s.cornerRadius = 8;
         s.sheen = false;
     }},
    {"Bold", [](Settings& s) {
         // High contrast: solid near-black, an accent outline and a chunky bar.
         // Presets only set the look. They never touch fonts, Size, position or behaviour.
         s.bgMode = BgCustom;
         s.bgColor = 0x0A0A0D;
         s.cornerRadius = 28;
         s.opacity = 100;
         s.borderMode = BorderAccent;
         s.barStyle = BarThick;
         s.sheen = false;
         s.animation = AnimGlide;
     }},
    {"Quiet", [](Settings& s) {
         // Barely there: small, see-through, no shadow or gloss, fades gently.
         s.layout = LayoutCompact;
         s.cornerRadius = 12;
         s.opacity = 78;
         s.borderMode = BorderNone;
         s.shadow = false;
         s.sheen = false;
         s.barStyle = BarThin;
         s.showKnob = false;
         s.showGlyph = false;
         s.animation = AnimFade;
         s.animSpeed = 70;
     }},
};

constexpr int kCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

} // namespace

int count() { return kCount; }

const char* name(int index) { return (index >= 0 && index < kCount) ? kPresets[index].name : ""; }

void apply(int index, Settings& settings) {
    if (index < 0 || index >= kCount) return;
    settings.resetStyle();
    if (index != 0) legacyBase(settings);
    kPresets[index].fn(settings);
    settings.clamp();
}

} // namespace osd::presets
