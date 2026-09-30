#pragma once

#include <cstdint>
#include <string>

namespace osd {

// Stored as numbers: never reorder, only append.
enum Position : int {
    TopLeft = 0, TopCenter, TopRight, BottomLeft, BottomCenter, BottomRight, // 1.0
    MiddleLeft, MiddleCenter, MiddleRight,                                   // 1.1
    PositionCount
};
enum MonitorMode : int { MonitorMain = 0, MonitorPrimary, MonitorCursor, MonitorModeCount };
enum LayoutKind : int { LayoutClassic = 0, LayoutCompact, LayoutBanner, LayoutPoster, LayoutCount };
enum ArtShape : int { ArtRounded = 0, ArtCircle, ArtSquare, ArtShapeCount };
enum BgMode : int { BgDark = 0, BgLight, BgCoverTint, BgCoverColour, BgCustom, BgModeCount };
enum TextMode : int { TextAuto = 0, TextCustom, TextModeCount };
enum BorderMode : int { BorderNone = 0, BorderSubtle, BorderAccent, BorderModeCount };
enum BarStyle : int { BarRounded = 0, BarThin, BarThick, BarStyleCount };
enum Animation : int { AnimFade = 0, AnimSlide, AnimGlide, AnimNone, AnimCount };

// Everything the user can change. A plain value type: the OSD takes a copy each time it shows,
// so a preview can use unsaved dialog values without touching the stored configuration.
// Stored as one key=value text blob (see config.cpp), so adding a field never breaks old profiles.
struct Settings {
    // Behaviour
    bool enabled = true;
    bool onTrack = true;
    bool onStreamTitle = true; // internet radio: the title changed under a running stream
    bool onPause = true;
    bool onSeek = false;
    bool onlyWhenUnfocused = false;
    bool hideInFullscreen = true;
    bool holdWhilePaused = false; // stay up while playback is paused
    bool waitForArt = true;       // a new track's card waits (briefly) for its cover
    bool fadeOnHover = true;      // turn nearly transparent while the mouse pointer is over it
    int seconds = 4;              // fully visible time (1-30)

    // Placement
    int position = TopRight;
    int monitor = MonitorMain; // 1.0 stored this as the followMonitor flag; parse() maps it
    int margin = 32;           // px at 100% size (0-400)
    int scale = 100;           // percent (50-300)
    int opacity = 100;         // card background, percent (30-100)

    // Elements
    bool showArt = true;
    bool showProgress = true;
    bool showTimes = true;
    bool showRemaining = false; // right-hand time is the time left, not the length
    bool showGlyph = true;
    bool showKnob = true;

    // Style (everything a preset sets)
    int layout = LayoutCompact;
    int artShape = ArtSquare;
    int cornerRadius = 8; // px at 100% (0-48)
    int bgMode = BgCustom;
    std::uint32_t bgColor = 0x000000; // BgCustom
    int textMode = TextAuto;
    std::uint32_t textColor = 0xFFFFFF; // TextCustom
    int borderMode = BorderNone;
    int barStyle = BarThin;
    bool shadow = true;
    bool sheen = false;
    bool accentFromCover = true;
    std::uint32_t accent = 0x4DA3FF;
    int animation = AnimSlide;
    int animSpeed = 100; // percent (40-300)

    // Default sizes of the two fonts: 11 pt bold title, 9 pt detail.
    static constexpr int kTitleDefaultTenthsPt = 110;
    static constexpr int kDetailDefaultTenthsPt = 90;
    // Picked with the standard Windows font dialog; presets never change them. Line 1 uses the
    // title font; lines 2-3 and the time labels use the detail font. Empty family = the user's UI
    // font (Columns UI, else Default UI; see host_font.h). Size is in tenths of a point and exact
    // (at 100% Size); 0 = the layout's own size. Weight 0 = default (bold title, regular detail),
    // else a LOGFONT weight (100-900).
    std::string titleFont;
    int titlePt = kTitleDefaultTenthsPt;
    int titleWeight = 0;
    bool titleItalic = false;
    std::string detailFont;
    int detailPt = kDetailDefaultTenthsPt;
    int detailWeight = 0;
    bool detailItalic = false;

    // Text (title formatting) and the fallback chain, kept across presets.
    std::string line1 = "$if2(%title%,%filename%)";
    std::string line2 = "%artist%";
    std::string line3 = "%album%[ '('%date%')']";
    std::string fallback1;
    std::string fallback2;
    std::string fallback3;

    //! The stored settings. Parsed once and kept; save() refreshes the copy. Main thread only.
    static const Settings& current();
    //! Stores these settings and makes them current(). Main thread only.
    void save() const;
    void clamp();
    //! Back to the default look. Leaves behaviour, placement, elements that are not part of the
    //! look, text lines and fonts alone.
    void resetStyle();

    std::string serialize() const;
    static Settings parse(const std::string& blob);
};

} // namespace osd
