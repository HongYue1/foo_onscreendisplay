// OSD_STANDALONE is defined only by the test programs in test/, which build without foobar2000:
// they use serialize/parse/clamp/presets but never the stored configuration.
#ifndef OSD_STANDALONE
#include <helpers/foobar2000+atl.h>
#include <SDK/cfg_var.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "config.h"
#include "guids.h"

namespace osd {
namespace {

// One text blob, key=value per line. cfg_var objects register at construction, so it is static.
#ifndef OSD_STANDALONE
cfg_string c_blob(guids::cfg_settings, "");
#endif

struct Writer {
    std::string out;
    void put(const char* k, const std::string& v) {
        out.append(k);
        out.push_back('=');
        out.append(v);
        out.push_back('\n');
    }
    void operator()(const char* k, bool& v) { put(k, v ? "1" : "0"); }
    void operator()(const char* k, int& v) { put(k, std::to_string(v)); }
    void operator()(const char* k, std::uint32_t& v) {
        char b[16];
        std::snprintf(b, sizeof b, "%06X", static_cast<unsigned>(v & 0xFFFFFFu));
        put(k, b);
    }
    void operator()(const char* k, std::string& v) {
        std::string t = v;
        for (char& c : t)
            if (c == '\n' || c == '\r') c = ' ';
        put(k, t);
    }
};

struct Reader {
    std::unordered_map<std::string, std::string> kv;
    const std::string* find(const char* k) const {
        auto it = kv.find(k);
        return it == kv.end() ? nullptr : &it->second;
    }
    void operator()(const char* k, bool& v) {
        if (auto* s = find(k)) v = (*s == "1");
    }
    void operator()(const char* k, int& v) {
        if (auto* s = find(k)) v = static_cast<int>(std::strtol(s->c_str(), nullptr, 10));
    }
    void operator()(const char* k, std::uint32_t& v) {
        if (auto* s = find(k)) v = static_cast<std::uint32_t>(std::strtoul(s->c_str(), nullptr, 16)) & 0xFFFFFFu;
    }
    void operator()(const char* k, std::string& v) {
        if (auto* s = find(k)) v = *s;
    }
};

template <class V>
void visit(Settings& s, V& v) {
    v("enabled", s.enabled);
    v("onTrack", s.onTrack);
    v("onStreamTitle", s.onStreamTitle);
    v("onPause", s.onPause);
    v("onSeek", s.onSeek);
    v("onlyWhenUnfocused", s.onlyWhenUnfocused);
    v("hideInFullscreen", s.hideInFullscreen);
    v("holdWhilePaused", s.holdWhilePaused);
    v("waitForArt", s.waitForArt);
    v("fadeOnHover", s.fadeOnHover);
    v("seconds", s.seconds);
    v("position", s.position);
    v("monitor", s.monitor);
    v("margin", s.margin);
    v("scale", s.scale);
    v("opacity", s.opacity);
    v("showArt", s.showArt);
    v("showProgress", s.showProgress);
    v("showTimes", s.showTimes);
    v("showRemaining", s.showRemaining);
    v("showGlyph", s.showGlyph);
    v("showKnob", s.showKnob);
    v("layout", s.layout);
    v("artShape", s.artShape);
    v("cornerRadius", s.cornerRadius);
    v("bgMode", s.bgMode);
    v("bgColor", s.bgColor);
    v("textMode", s.textMode);
    v("textColor", s.textColor);
    v("borderMode", s.borderMode);
    v("barStyle", s.barStyle);
    v("shadow", s.shadow);
    v("sheen", s.sheen);
    v("accentFromCover", s.accentFromCover);
    v("accent", s.accent);
    v("animation", s.animation);
    v("animSpeed", s.animSpeed);
    v("titleFont", s.titleFont);
    v("detailFont", s.detailFont);
    v("titlePt", s.titlePt);
    v("titleWeight", s.titleWeight);
    v("titleItalic", s.titleItalic);
    v("detailPt", s.detailPt);
    v("detailWeight", s.detailWeight);
    v("detailItalic", s.detailItalic);
    v("line1", s.line1);
    v("line2", s.line2);
    v("line3", s.line3);
    v("fallback1", s.fallback1);
    v("fallback2", s.fallback2);
    v("fallback3", s.fallback3);
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

void Settings::clamp() {
    seconds = clampInt(seconds, 1, 30);
    position = clampInt(position, 0, PositionCount - 1);
    monitor = clampInt(monitor, 0, MonitorModeCount - 1);
    margin = clampInt(margin, 0, 400);
    scale = clampInt(scale, 50, 300);
    opacity = clampInt(opacity, 30, 100);
    layout = clampInt(layout, 0, LayoutCount - 1);
    artShape = clampInt(artShape, 0, ArtShapeCount - 1);
    cornerRadius = clampInt(cornerRadius, 0, 48);
    bgMode = clampInt(bgMode, 0, BgModeCount - 1);
    textMode = clampInt(textMode, 0, TextModeCount - 1);
    borderMode = clampInt(borderMode, 0, BorderModeCount - 1);
    barStyle = clampInt(barStyle, 0, BarStyleCount - 1);
    animation = clampInt(animation, 0, AnimCount - 1);
    animSpeed = clampInt(animSpeed, 40, 300);
    if (titlePt != 0) titlePt = clampInt(titlePt, 60, 400);
    if (detailPt != 0) detailPt = clampInt(detailPt, 60, 400);
    if (titleWeight != 0) titleWeight = clampInt(titleWeight, 100, 900);
    if (detailWeight != 0) detailWeight = clampInt(detailWeight, 100, 900);
    bgColor &= 0xFFFFFFu;
    textColor &= 0xFFFFFFu;
    accent &= 0xFFFFFFu;
}

void Settings::resetStyle() {
    const Settings d;
    opacity = d.opacity;
    showArt = d.showArt;
    showProgress = d.showProgress;
    showTimes = d.showTimes;
    showGlyph = d.showGlyph;
    showKnob = d.showKnob;
    layout = d.layout;
    artShape = d.artShape;
    cornerRadius = d.cornerRadius;
    bgMode = d.bgMode;
    bgColor = d.bgColor;
    textMode = d.textMode;
    textColor = d.textColor;
    borderMode = d.borderMode;
    barStyle = d.barStyle;
    shadow = d.shadow;
    sheen = d.sheen;
    accentFromCover = d.accentFromCover;
    accent = d.accent;
    animation = d.animation;
    animSpeed = d.animSpeed;
}

std::string Settings::serialize() const {
    Settings copy = *this;
    copy.clamp();
    Writer w;
    visit(copy, w);
    return w.out;
}

Settings Settings::parse(const std::string& blob) {
    Reader r;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t end = blob.find('\n', pos);
        if (end == std::string::npos) end = blob.size();
        const size_t eq = blob.find('=', pos);
        if (eq != std::string::npos && eq < end) r.kv[blob.substr(pos, eq - pos)] = blob.substr(eq + 1, end - eq - 1);
        pos = end + 1;
    }
    Settings s;
    visit(s, r);
    // 1.0 had a followMonitor flag where 1.1 has a monitor choice.
    if (r.find("monitor") == nullptr) {
        if (const std::string* follow = r.find("followMonitor")) s.monitor = *follow == "0" ? MonitorPrimary : MonitorMain;
    }
    s.clamp();
    return s;
}

#ifndef OSD_STANDALONE
namespace {
Settings g_current;
bool g_currentLoaded = false;
} // namespace

// cfg_var values are read from the profile before any initquit runs, so the first call (from
// on_init at the earliest) already sees the stored blob.
const Settings& Settings::current() {
    if (!g_currentLoaded) {
        const pfc::string8 blob = c_blob.get();
        g_current = blob.is_empty() ? Settings{} : parse(blob.c_str());
        g_currentLoaded = true;
    }
    return g_current;
}

void Settings::save() const {
    const std::string blob = serialize();
    c_blob = blob.c_str();
    g_current = parse(blob);
    g_currentLoaded = true;
}
#endif

} // namespace osd
