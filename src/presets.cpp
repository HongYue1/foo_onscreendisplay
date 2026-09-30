// OSD_STANDALONE (the offline tests) keeps user presets in memory only.
#ifndef OSD_STANDALONE
#include <helpers/foobar2000+atl.h>
#include <SDK/cfg_var.h>
#endif

#include "presets.h"

#include <cstring>
#include <string>
#include <vector>

#include "guids.h"

namespace osd::presets {
namespace {

// A preset is a look: layout, surface, colours, motion and which card elements show. It never
// touches behaviour, placement, size, the time format, the text lines or the fonts.
//
// Every preset but Midnight starts from base(), a dark Classic card, so each one only lists what
// makes it different and no preset inherits a leftover from the one applied before it.
void base(Settings& s) {
    s.layout = LayoutClassic;
    s.artShape = ArtRounded;
    s.cornerRadius = 16;
    s.bgMode = BgDark;
    s.bgColor = 0x15151A;
    s.opacity = 94;
    s.textMode = TextAuto;
    s.textColor = 0xFFFFFF;
    s.borderMode = BorderSubtle;
    s.barStyle = BarRounded;
    s.shadow = true;
    s.sheen = false;
    s.accentFromCover = true;
    s.accent = 0x4DA3FF;
    s.animation = AnimSlide;
    s.animSpeed = 100;
    s.showArt = true;
    s.showProgress = true;
    s.showTimes = true;
    s.showGlyph = true;
    s.showKnob = true;
}

// A fixed colour scheme: its own card, accent and text colour, whatever the cover.
void theme(Settings& s, std::uint32_t bg, std::uint32_t accent, std::uint32_t text) {
    s.bgMode = BgCustom;
    s.bgColor = bg;
    s.opacity = 97;
    s.accentFromCover = false;
    s.accent = accent;
    s.textMode = TextCustom;
    s.textColor = text;
}

struct Preset {
    const char* name;
    void (*fn)(Settings&);
};

// Menu order: the default, dark basics, light cards, cover-driven, layouts, colour themes.
const Preset kPresets[] = {
    {"Midnight (default)", nullptr}, // exactly Settings' own defaults

    // ---- dark basics ------------------------------------------------------------------------
    {"Graphite", [](Settings& s) {
         // The classic card: soft charcoal, fine edge, a touch of gloss.
         s.sheen = true;
     }},
    {"Glass", [](Settings& s) {
         // A frosted, see-through pane. Opaque enough to stay readable over a bright desktop.
         s.opacity = 70;
         s.cornerRadius = 20;
         s.sheen = true;
         s.animation = AnimFade;
     }},
    {"AMOLED", [](Settings& s) {
         s.bgMode = BgCustom;
         s.bgColor = 0x000000;
         s.opacity = 100;
         s.borderMode = BorderNone;
         s.shadow = false;
         s.cornerRadius = 14;
         s.barStyle = BarThin;
         s.showKnob = false;
         s.animation = AnimFade;
     }},
    {"Flat", [](Settings& s) {
         s.bgMode = BgCustom;
         s.bgColor = 0x1C1C20;
         s.opacity = 100;
         s.cornerRadius = 0;
         s.artShape = ArtSquare;
         s.borderMode = BorderNone;
         s.shadow = false;
         s.barStyle = BarThin;
         s.showKnob = false;
         s.animation = AnimFade;
     }},
    {"Compact", [](Settings& s) {
         // One tight row of art and text with a hairline bar.
         s.layout = LayoutCompact;
         s.cornerRadius = 12;
         s.barStyle = BarThin;
         s.showKnob = false;
     }},
    {"Minimal", [](Settings& s) {
         // Just the words and a hairline: no cover, icon or times.
         s.layout = LayoutCompact;
         s.cornerRadius = 10;
         s.opacity = 90;
         s.borderMode = BorderNone;
         s.barStyle = BarThin;
         s.showArt = false;
         s.showGlyph = false;
         s.showTimes = false;
         s.showKnob = false;
         s.animation = AnimFade;
     }},
    {"Quiet", [](Settings& s) {
         // Barely there: small, see-through, no shadow, fades gently.
         s.layout = LayoutCompact;
         s.cornerRadius = 12;
         s.opacity = 78;
         s.borderMode = BorderNone;
         s.shadow = false;
         s.barStyle = BarThin;
         s.showKnob = false;
         s.showGlyph = false;
         s.animation = AnimFade;
         s.animSpeed = 70;
     }},

    // ---- light ------------------------------------------------------------------------------
    {"Snow", [](Settings& s) {
         s.bgMode = BgLight;
         s.opacity = 97;
     }},
    {"Frost", [](Settings& s) {
         // Glass, in white.
         s.bgMode = BgLight;
         s.opacity = 80;
         s.cornerRadius = 20;
         s.sheen = true;
         s.animation = AnimFade;
     }},
    {"Paper", [](Settings& s) {
         // Warm off-white with ink-coloured text and a terracotta accent.
         theme(s, 0xF7F3EA, 0xB5542D, 0x2B2926);
         s.opacity = 100;
         s.cornerRadius = 4;
         s.artShape = ArtSquare;
         s.barStyle = BarThin;
         s.showKnob = false;
         s.animation = AnimFade;
     }},

    // ---- from the cover ---------------------------------------------------------------------
    {"Cover colour", [](Settings& s) {
         // The card takes the cover's colour; text and bar stay neutral on top of it.
         s.bgMode = BgCoverColour;
         s.opacity = 100;
         s.cornerRadius = 18;
         s.borderMode = BorderNone;
         s.animation = AnimGlide;
     }},
    {"Cover tint", [](Settings& s) {
         // A dark card washed with the cover's colour; the bar takes the same colour.
         s.bgMode = BgCoverTint;
         s.opacity = 96;
         s.barStyle = BarThin;
         s.showKnob = false;
     }},
    {"Spotlight", [](Settings& s) {
         // A big cover on a card of its own colour.
         s.layout = LayoutPoster;
         s.bgMode = BgCoverColour;
         s.opacity = 100;
         s.cornerRadius = 24;
         s.borderMode = BorderNone;
         s.animation = AnimGlide;
     }},

    // ---- layouts ----------------------------------------------------------------------------
    {"Banner", [](Settings& s) {
         // Wide and low, for the top or bottom edge of the screen.
         s.layout = LayoutBanner;
         s.cornerRadius = 12;
         s.barStyle = BarThin;
         s.showKnob = false;
     }},
    {"Poster", [](Settings& s) {
         s.layout = LayoutPoster;
         s.cornerRadius = 22;
         s.animation = AnimGlide;
     }},
    {"Circle", [](Settings& s) {
         // A round cover like a record label, in a softly rounded card.
         s.layout = LayoutCompact;
         s.artShape = ArtCircle;
         s.cornerRadius = 28;
     }},
    {"Pill", [](Settings& s) {
         // A capsule: round cover and text only, no progress bar.
         s.layout = LayoutCompact;
         s.artShape = ArtCircle;
         s.cornerRadius = 48;
         s.opacity = 96;
         s.showProgress = false;
     }},

    // ---- colour themes ----------------------------------------------------------------------
    {"Neon", [](Settings& s) {
         theme(s, 0x0B0B16, 0xFF3CAC, 0xFFFFFF);
         s.borderMode = BorderAccent;
         s.barStyle = BarThick;
         s.cornerRadius = 14;
         s.animation = AnimGlide;
     }},
    {"Bold", [](Settings& s) {
         // High contrast: solid near-black, an outline in the cover's colour, a chunky bar.
         s.bgMode = BgCustom;
         s.bgColor = 0x0A0A0D;
         s.opacity = 100;
         s.cornerRadius = 24;
         s.borderMode = BorderAccent;
         s.barStyle = BarThick;
         s.animation = AnimGlide;
     }},
    {"Retro terminal", [](Settings& s) {
         theme(s, 0x050A05, 0x33FF66, 0x33FF66);
         s.cornerRadius = 2;
         s.borderMode = BorderAccent;
         s.shadow = false;
         s.showArt = false;
         s.barStyle = BarThin;
         s.showKnob = false;
         s.animation = AnimFade;
     }},
    {"Sunset", [](Settings& s) {
         theme(s, 0x2A1220, 0xFF7A59, 0xFFE6D9);
         s.borderMode = BorderAccent;
         s.cornerRadius = 22;
         s.animation = AnimGlide;
     }},
    {"Ocean", [](Settings& s) {
         theme(s, 0x0B1D2E, 0x3CC6FF, 0xE4F5FF);
         s.opacity = 92;
         s.barStyle = BarThick;
     }},
    {"Forest", [](Settings& s) {
         theme(s, 0x0F1F16, 0x7BD88F, 0xE6F4E9);
         s.cornerRadius = 12;
         s.artShape = ArtSquare;
         s.barStyle = BarThin;
         s.showKnob = false;
         s.animation = AnimFade;
     }},
    {"Rose", [](Settings& s) {
         theme(s, 0x2A1522, 0xFF6FA5, 0xFFE6EF);
         s.layout = LayoutCompact;
         s.artShape = ArtCircle;
         s.borderMode = BorderAccent;
         s.cornerRadius = 28;
     }},
    {"Nord", [](Settings& s) {
         theme(s, 0x2E3440, 0x88C0D0, 0xECEFF4);
         s.cornerRadius = 10;
     }},
    {"Dracula", [](Settings& s) {
         theme(s, 0x282A36, 0xBD93F9, 0xF8F8F2);
         s.cornerRadius = 12;
         s.barStyle = BarThick;
     }},
    {"Solarized", [](Settings& s) {
         theme(s, 0x002B36, 0xB58900, 0xEEE8D5);
         s.cornerRadius = 6;
         s.artShape = ArtSquare;
         s.barStyle = BarThin;
         s.showKnob = false;
     }},
    {"Gruvbox", [](Settings& s) {
         theme(s, 0x282828, 0xFE8019, 0xEBDBB2);
         s.cornerRadius = 8;
         s.artShape = ArtSquare;
         s.barStyle = BarThick;
     }},
    {"Catppuccin", [](Settings& s) {
         theme(s, 0x1E1E2E, 0xCBA6F7, 0xCDD6F4);
         s.cornerRadius = 18;
         s.animation = AnimGlide;
     }},
    {"Tokyo Night", [](Settings& s) {
         theme(s, 0x1A1B26, 0x7AA2F7, 0xC0CAF5);
         s.cornerRadius = 14;
         s.layout = LayoutCompact;
     }},
};

constexpr int kCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

// ---- user presets ------------------------------------------------------------------------------
// Stored as one text blob: each preset is a line "@name=<name>" followed by its look as
// key=value lines (Settings::serialize, so unknown or missing keys behave like the main blob).

struct User {
    std::string name;
    Settings look; // defaults everywhere but the look
};

#ifndef OSD_STANDALONE
cfg_string c_users(guids::cfg_user_presets, "");
#endif

std::vector<User> g_users;
bool g_loaded = false;

constexpr size_t kMaxName = 64;

char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool sameName(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i)
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    return i == a.size() && b[i] == '\0';
}

// Control characters out (a name is one line), outer spaces trimmed, at most kMaxName bytes
// without cutting a UTF-8 sequence.
std::string cleanName(const std::string& raw) {
    std::string s;
    for (char c : raw) s.push_back(static_cast<unsigned char>(c) < 0x20 ? ' ' : c);
    const size_t b = s.find_first_not_of(' ');
    if (b == std::string::npos) return {};
    s = s.substr(b, s.find_last_not_of(' ') - b + 1);
    if (s.size() > kMaxName) {
        size_t n = kMaxName;
        while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
        s.resize(n);
        while (!s.empty() && s.back() == ' ') s.pop_back();
    }
    return s;
}

bool isBuiltinName(const std::string& n) {
    for (int i = 0; i < kCount; ++i) {
        // "Midnight (default)" is also taken as plain "Midnight".
        const char* b = kPresets[i].name;
        if (sameName(n, b)) return true;
        const char* paren = std::strstr(b, " (");
        if (paren != nullptr && sameName(n, std::string(b, paren).c_str())) return true;
    }
    return false;
}

void load() {
    if (g_loaded) return;
    g_loaded = true;
#ifndef OSD_STANDALONE
    const std::string blob = c_users.get().c_str();
    std::string name, body;
    auto flush = [&] {
        name = cleanName(name);
        if (name.empty() || isBuiltinName(name)) return;
        for (const User& u : g_users)
            if (sameName(u.name, name.c_str())) return;
        User u;
        u.name = name;
        u.look.copyLook(Settings::parse(body));
        g_users.push_back(std::move(u));
    };
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t end = blob.find('\n', pos);
        if (end == std::string::npos) end = blob.size();
        const std::string line = blob.substr(pos, end - pos);
        pos = end + 1;
        if (line.compare(0, 6, "@name=") == 0) {
            flush();
            name = line.substr(6);
            body.clear();
        } else {
            body.append(line).push_back('\n');
        }
    }
    flush();
#endif
}

void store() {
#ifndef OSD_STANDALONE
    std::string blob;
    for (const User& u : g_users) blob.append("@name=").append(u.name).append("\n").append(u.look.serialize());
    c_users = blob.c_str();
#endif
}

} // namespace

int builtinCount() { return kCount; }

int count() {
    load();
    return kCount + static_cast<int>(g_users.size());
}

bool isUser(int index) { return index >= kCount && index < count(); }

const char* name(int index) {
    if (index >= 0 && index < kCount) return kPresets[index].name;
    return isUser(index) ? g_users[static_cast<size_t>(index - kCount)].name.c_str() : "";
}

void apply(int index, Settings& settings) {
    if (index >= 0 && index < kCount) {
        settings.resetStyle();
        if (kPresets[index].fn != nullptr) {
            base(settings);
            kPresets[index].fn(settings);
        }
    } else if (isUser(index)) {
        settings.copyLook(g_users[static_cast<size_t>(index - kCount)].look);
    } else {
        return;
    }
    settings.clamp();
}

namespace {
bool sameLook(int index, const Settings& clamped, const std::string& current) {
    Settings candidate = clamped;
    apply(index, candidate);
    return candidate.serialize() == current;
}
} // namespace

bool matches(int index, const Settings& settings) {
    if (index < 0 || index >= count()) return false;
    Settings clamped = settings;
    clamped.clamp();
    return sameLook(index, clamped, clamped.serialize());
}

int match(const Settings& settings) {
    Settings clamped = settings;
    clamped.clamp();
    const std::string current = clamped.serialize();
    for (int i = 0; i < count(); ++i)
        if (sameLook(i, clamped, current)) return i;
    return -1;
}

int find(const std::string& rawName) {
    const std::string n = cleanName(rawName);
    if (n.empty()) return -1;
    for (int i = 0; i < count(); ++i)
        if (sameName(n, name(i))) return i;
    return -1;
}

bool validUserName(const std::string& rawName) {
    const std::string n = cleanName(rawName);
    return !n.empty() && !isBuiltinName(n);
}

int saveUser(const std::string& rawName, const Settings& settings) {
    const std::string n = cleanName(rawName);
    if (n.empty() || isBuiltinName(n)) return -1;
    load();
    User u;
    u.name = n;
    u.look.copyLook(settings);
    u.look.clamp();
    int index = find(n);
    if (isUser(index)) {
        g_users[static_cast<size_t>(index - kCount)] = std::move(u);
    } else {
        g_users.push_back(std::move(u));
        index = count() - 1;
    }
    store();
    return index;
}

bool removeUser(int index) {
    if (!isUser(index)) return false;
    g_users.erase(g_users.begin() + (index - kCount));
    store();
    return true;
}

} // namespace osd::presets
