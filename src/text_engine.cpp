#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <memory>
#include <unordered_map>

#include "text_engine.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")

namespace G = Gdiplus;

namespace osd::text {
namespace {

struct FamilyInfo {
    std::wstring name;
    std::unique_ptr<G::FontFamily> family; // null when not installed / not usable by GDI+
    HFONT hfont = nullptr;                 // for glyph coverage tests only
};

std::unordered_map<std::wstring, FamilyInfo>& cache() {
    static std::unordered_map<std::wstring, FamilyInfo> c;
    return c;
}

HDC g_dc = nullptr;
std::unique_ptr<G::StringFormat> g_format;

std::wstring lowered(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) c = static_cast<wchar_t>(std::towlower(c));
    return r;
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Returns null when the family is not usable. Cached either way, so a missing font costs one lookup.
FamilyInfo* lookup(const std::wstring& name) {
    if (name.empty()) return nullptr;
    const std::wstring key = lowered(name);
    auto& c = cache();
    auto it = c.find(key);
    if (it == c.end()) {
        FamilyInfo fi;
        fi.name = name;
        auto fam = std::make_unique<G::FontFamily>(name.c_str());
        if (fam->GetLastStatus() == G::Ok && fam->IsAvailable()) fi.family = std::move(fam);
        it = c.emplace(key, std::move(fi)).first;
    }
    return it->second.family ? &it->second : nullptr;
}

HFONT coverageFont(FamilyInfo& fi) {
    if (!fi.hfont) {
        fi.hfont = CreateFontW(-32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fi.name.c_str());
    }
    return fi.hfont;
}

G::StringFormat* format() {
    if (!g_format) {
        g_format.reset(G::StringFormat::GenericTypographic()->Clone());
        g_format->SetFormatFlags(G::StringFormatFlagsMeasureTrailingSpaces | G::StringFormatFlagsNoWrap |
                                 G::StringFormatFlagsNoClip);
    }
    return g_format.get();
}

bool isHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool isLowSurrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

// Characters that take the font of their neighbour instead of picking one: they have no glyph of
// their own worth splitting a run for.
bool isNeutral(wchar_t c) {
    return c == L' ' || c == 0x00A0 || c == 0x200C || c == 0x200D || c == 0xFE0E || c == 0xFE0F ||
           (c >= 0x0300 && c <= 0x036F) || (c >= 0x2000 && c <= 0x200B);
}

const wchar_t* const kSystemFallbacks[] = {
    L"Segoe UI",           L"Segoe UI Symbol",     L"Segoe UI Emoji",       L"Microsoft YaHei UI", L"Yu Gothic UI",
    L"Malgun Gothic",      L"Microsoft JhengHei UI", L"Nirmala UI",         L"Leelawadee UI",      L"Segoe UI Historic",
    L"SimSun-ExtB",        L"MingLiU-ExtB",        L"Arial Unicode MS",     L"Arial",              L"Tahoma",
};

// Where a character outside the BMP goes. GetGlyphIndicesW only understands UTF-16 units, so
// these cannot be coverage-tested like the rest and are routed by range instead.
enum class Plane1 { Emoji, Ideograph, Other };
Plane1 classify(std::uint32_t cp) {
    if ((cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x1FC00 && cp <= 0x1FFFF)) return Plane1::Emoji;
    if (cp >= 0x20000 && cp <= 0x3FFFF) return Plane1::Ideograph; // CJK extensions B and later
    return Plane1::Other;                                         // maths alphanumerics, historic scripts
}

} // namespace

struct Faces::Impl {
    struct Face {
        FamilyInfo* info = nullptr;
        std::unique_ptr<G::Font> font;
        float ascent = 0.f;
        float descent = 0.f;
    };
    std::vector<Face> faces; // the chain, primary first
    int emojiFace = -1;
    int symbolFace = -1;
    int ideographFace = -1; // SimSun-ExtB / MingLiU-ExtB
    int historicFace = -1;
    float px = 12.f;
    int style = G::FontStyleRegular;
    std::wstring key; // what init() was last called with; the same call again is a no-op

    int supplementaryFace(std::uint32_t cp) const {
        int f = -1;
        switch (classify(cp)) {
        case Plane1::Emoji: f = emojiFace >= 0 ? emojiFace : symbolFace; break;
        case Plane1::Ideograph: f = ideographFace; break;
        case Plane1::Other: f = symbolFace >= 0 ? symbolFace : historicFace; break;
        }
        return f >= 0 ? f : 0;
    }

    Face& ensure(size_t i) {
        Face& f = faces[i];
        if (!f.font) {
            G::FontFamily* fam = f.info->family.get();
            int st = style;
            if (!fam->IsStyleAvailable(st)) st = G::FontStyleRegular;
            if (!fam->IsStyleAvailable(st)) st = fam->IsStyleAvailable(G::FontStyleBold) ? G::FontStyleBold : G::FontStyleItalic;
            f.font = std::make_unique<G::Font>(fam, px, st, G::UnitPixel);
            const float em = static_cast<float>(fam->GetEmHeight(st));
            f.ascent = static_cast<float>(fam->GetCellAscent(st)) * px / em;
            f.descent = static_cast<float>(fam->GetCellDescent(st)) * px / em;
        }
        return f;
    }

    // Picks a chain index for every UTF-16 unit.
    void assign(const std::wstring& t, std::vector<int>& idx) {
        const size_t n = t.size();
        idx.assign(n, -1);

        bool ascii = true;
        for (wchar_t c : t)
            if (c >= 0x80) {
                ascii = false;
                break;
            }
        if (ascii) {
            idx.assign(n, 0);
            return;
        }

        // Anything outside the BMP is routed by range (see classify).
        size_t remaining = 0;
        for (size_t i = 0; i < n; ++i) {
            if (isHighSurrogate(t[i]) && i + 1 < n && isLowSurrogate(t[i + 1])) {
                const std::uint32_t cp = 0x10000u + ((static_cast<std::uint32_t>(t[i]) - 0xD800u) << 10) +
                                         (static_cast<std::uint32_t>(t[i + 1]) - 0xDC00u);
                idx[i] = idx[i + 1] = supplementaryFace(cp);
                ++i;
            } else if (isNeutral(t[i])) {
                idx[i] = -2; // resolved from a neighbour below
            } else {
                ++remaining;
            }
        }

        std::vector<WORD> glyphs(n);
        if (!g_dc) g_dc = CreateCompatibleDC(nullptr);
        for (size_t c = 0; c < faces.size() && remaining > 0; ++c) {
            HFONT hf = coverageFont(*faces[c].info);
            if (!hf) continue;
            HGDIOBJ old = SelectObject(g_dc, hf);
            const DWORD r = GetGlyphIndicesW(g_dc, t.c_str(), static_cast<int>(n), glyphs.data(), GGI_MARK_NONEXISTING_GLYPHS);
            SelectObject(g_dc, old);
            if (r == GDI_ERROR) continue;
            for (size_t i = 0; i < n; ++i) {
                if (idx[i] == -1 && glyphs[i] != 0xFFFF) {
                    idx[i] = static_cast<int>(c);
                    --remaining;
                }
            }
        }
        // Neutral characters follow the previous character (or the next, at the start).
        for (size_t i = 0; i < n; ++i) {
            if (idx[i] != -2) continue;
            int f = -1;
            if (i > 0 && idx[i - 1] >= 0) f = idx[i - 1];
            for (size_t j = i + 1; f < 0 && j < n; ++j)
                if (idx[j] >= 0) f = idx[j];
            idx[i] = f < 0 ? 0 : f;
        }
        for (auto& v : idx)
            if (v < 0) v = 0; // nobody has it: primary font draws its .notdef
    }

    float measure(G::Graphics& g, size_t face, const wchar_t* s, int n) {
        if (n <= 0) return 0.f;
        G::RectF box;
        g.MeasureString(s, n, ensure(face).font.get(), G::PointF(0.f, 0.f), format(), &box);
        return box.Width;
    }
};

Faces::Faces() : m(std::make_unique<Impl>()) {}
Faces::~Faces() = default;

void Faces::init(const std::string& primaryUtf8, const std::vector<std::string>& fallbacksUtf8, float emPixels, bool bold, bool italic) {
    Impl& s = *m;
    const float px = emPixels < 1.f ? 1.f : emPixels;
    const int style = (bold ? G::FontStyleBold : 0) | (italic ? G::FontStyleItalic : 0);

    std::vector<std::wstring> names;
    names.push_back(toWide(primaryUtf8));
    for (const auto& f : fallbacksUtf8) names.push_back(toWide(f));

    // Every card calls this; the fonts only need rebuilding when something actually changed.
    std::wstring key;
    for (const auto& n : names) (key += n) += L'\x1F';
    wchar_t tail[48];
    swprintf_s(tail, L"%.3f|%d", px, style);
    key += tail;
    if (key == s.key && !s.faces.empty()) return;
    s.key = std::move(key);

    s.faces.clear();
    s.emojiFace = s.symbolFace = s.ideographFace = s.historicFace = -1;
    s.px = px;
    s.style = style;
    for (const wchar_t* d : kSystemFallbacks) names.push_back(d);

    std::vector<std::wstring> seen;
    for (const auto& n : names) {
        FamilyInfo* fi = lookup(n);
        if (!fi) continue;
        const std::wstring key = lowered(fi->name);
        bool dup = false;
        for (const auto& k : seen) dup = dup || k == key;
        if (dup) continue;
        seen.push_back(key);
        Impl::Face face;
        face.info = fi;
        s.faces.push_back(std::move(face));
        const int at = static_cast<int>(s.faces.size()) - 1;
        if (key == L"segoe ui emoji") s.emojiFace = at;
        else if (key == L"segoe ui symbol") s.symbolFace = at;
        else if (key == L"segoe ui historic") s.historicFace = at;
        else if (s.ideographFace < 0 && (key == L"simsun-extb" || key == L"mingliu-extb")) s.ideographFace = at;
    }
    if (s.faces.empty()) {
        // No usable family at all (not expected on Windows): the generic family always exists.
        static FamilyInfo generic;
        if (!generic.family) {
            generic.name = L"Arial";
            generic.family.reset(G::FontFamily::GenericSansSerif()->Clone());
        }
        Impl::Face face;
        face.info = &generic;
        s.faces.push_back(std::move(face));
    }
}

float Faces::draw(G::Graphics& g, const std::wstring& text, float x, float y, float w, float h, const G::Brush& brush,
                  bool alignRight) {
    Impl& s = *m;
    if (text.empty() || w <= 0.f || s.faces.empty()) return 0.f;

    std::vector<int> idx;
    s.assign(text, idx);

    struct Part {
        size_t start;
        int len;
        int face;
        float width;
    };
    std::vector<Part> parts;
    const size_t n = text.size();
    for (size_t i = 0; i < n;) {
        size_t j = i + 1;
        while (j < n && idx[j] == idx[i]) ++j;
        Part p{i, static_cast<int>(j - i), idx[i], 0.f};
        p.width = s.measure(g, static_cast<size_t>(p.face), text.c_str() + i, p.len);
        parts.push_back(p);
        i = j;
    }
    float total = 0.f;
    for (const auto& p : parts) total += p.width;

    // Truncate: keep what fits beside an ellipsis.
    std::wstring ellipsis = L"\u2026";
    float ellipsisWidth = 0.f;
    if (total > w + 0.5f) {
        ellipsisWidth = s.measure(g, 0, ellipsis.c_str(), 1);
        const float limit = w - ellipsisWidth;
        std::vector<Part> kept;
        float acc = 0.f;
        for (const auto& p : parts) {
            if (acc + p.width <= limit) {
                kept.push_back(p);
                acc += p.width;
                continue;
            }
            // Longest prefix of this run that still fits.
            int lo = 0, hi = p.len;
            while (lo < hi) {
                const int mid = (lo + hi + 1) / 2;
                int cut = mid;
                if (cut < p.len && isLowSurrogate(text[p.start + static_cast<size_t>(cut)])) --cut;
                if (acc + s.measure(g, static_cast<size_t>(p.face), text.c_str() + p.start, cut) <= limit)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            int cut = lo;
            if (cut > 0 && cut < p.len && isLowSurrogate(text[p.start + static_cast<size_t>(cut)])) --cut;
            // Do not leave a dangling space before the ellipsis.
            while (cut > 0 && text[p.start + static_cast<size_t>(cut) - 1] == L' ') --cut;
            if (cut > 0) {
                Part q = p;
                q.len = cut;
                q.width = s.measure(g, static_cast<size_t>(p.face), text.c_str() + p.start, cut);
                kept.push_back(q);
                acc += q.width;
            }
            break;
        }
        parts = std::move(kept);
        total = acc + ellipsisWidth;
    } else {
        ellipsis.clear();
    }

    // One baseline for every run: each face is placed by its own ascent.
    Impl::Face& primary = s.ensure(0);
    const float baseline = y + (h - (primary.ascent + primary.descent)) / 2.f + primary.ascent;
    float cx = alignRight ? x + w - total : x;
    if (cx < x) cx = x;
    for (const auto& p : parts) {
        Impl::Face& f = s.ensure(static_cast<size_t>(p.face));
        g.DrawString(text.c_str() + p.start, p.len, f.font.get(), G::PointF(cx, baseline - f.ascent), format(), &brush);
        cx += p.width;
    }
    if (!ellipsis.empty()) g.DrawString(ellipsis.c_str(), 1, primary.font.get(), G::PointF(cx, baseline - primary.ascent), format(), &brush);
    return total;
}

void shutdown() {
    for (auto& kv : cache()) {
        if (kv.second.hfont) DeleteObject(kv.second.hfont);
        kv.second.hfont = nullptr;
    }
    cache().clear();
    if (g_dc) {
        DeleteDC(g_dc);
        g_dc = nullptr;
    }
    g_format.reset();
}

} // namespace osd::text
