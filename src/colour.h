#pragma once

// Colour maths in OKLab / OKLCh (Bjoern Ottosson, 2020): a perceptual space where equal steps look
// equal, lightness means what the eye sees (a blue and a yellow of the same L really are equally
// light) and chroma measures colourfulness without HSV's habit of calling dark mud "saturated".
// Used to pick the cover's accent (artwork.cpp) and to make it legible on the card
// (osd_window.cpp). Header-only, no Windows dependency.

#include <cmath>
#include <cstdint>

namespace osd::colour {

struct Lab {
    float L = 0.f; // 0 black .. 1 white
    float a = 0.f; // green .. red
    float b = 0.f; // blue .. yellow
};

inline float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float linearToSrgb(float c) { return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f; }

inline Lab linearToOklab(float r, float g, float b) {
    const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return Lab{0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
               1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
               0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

inline void oklabToLinear(const Lab& c, float& r, float& g, float& b) {
    const float l0 = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
    const float m0 = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
    const float s0 = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
    const float l = l0 * l0 * l0, m = m0 * m0 * m0, s = s0 * s0 * s0;
    r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

inline Lab fromRgb(std::uint32_t rgb) {
    return linearToOklab(srgbToLinear(static_cast<float>((rgb >> 16) & 0xFF) / 255.f),
                         srgbToLinear(static_cast<float>((rgb >> 8) & 0xFF) / 255.f),
                         srgbToLinear(static_cast<float>(rgb & 0xFF) / 255.f));
}

inline bool inGamut(const Lab& c) {
    float r, g, b;
    oklabToLinear(c, r, g, b);
    constexpr float e = 1e-4f;
    return r >= -e && r <= 1.f + e && g >= -e && g <= 1.f + e && b >= -e && b <= 1.f + e;
}

inline std::uint32_t toRgb(const Lab& c) {
    float lin[3];
    oklabToLinear(c, lin[0], lin[1], lin[2]);
    std::uint32_t out = 0;
    for (float v : lin) {
        v = linearToSrgb(v < 0.f ? 0.f : (v > 1.f ? 1.f : v));
        out = (out << 8) | static_cast<std::uint32_t>(v * 255.f + 0.5f);
    }
    return out;
}

inline float chroma(const Lab& c) { return std::sqrt(c.a * c.a + c.b * c.b); }
inline float hue(const Lab& c) { return std::atan2(c.b, c.a); } // radians

//! L, C, h to sRGB. Keeps lightness and hue and gives up chroma until the colour exists in sRGB,
//! so a vivid hue at an unusual lightness comes out as the most colourful version that can be shown.
inline std::uint32_t fromLch(float L, float C, float h) {
    const float ca = std::cos(h), sa = std::sin(h);
    Lab c{L, C * ca, C * sa};
    if (!inGamut(c)) {
        float lo = 0.f, hi = C;
        for (int i = 0; i < 18; ++i) {
            const float mid = (lo + hi) * 0.5f;
            (inGamut(Lab{L, mid * ca, mid * sa}) ? lo : hi) = mid;
        }
        c = Lab{L, lo * ca, lo * sa};
    }
    return toRgb(c);
}

//! Below this chroma a colour reads as grey.
inline constexpr float kGreyChroma = 0.035f;

//! The cover's colour made legible as an accent (bar, outline, glyph) on a dark or light card:
//! the same hue, at a lightness that stands out from the card, and colourful enough to read as a
//! colour. A grey cover gets an off-white (dark card) or charcoal (light card).
inline std::uint32_t accentForCard(std::uint32_t rgb, bool lightCard) {
    const Lab c = fromRgb(rgb);
    const float C = chroma(c);
    if (C < kGreyChroma) {
        const float L = lightCard ? (c.L < 0.36f ? c.L : 0.36f) : (c.L > 0.87f ? c.L : 0.87f);
        return toRgb(Lab{L, 0.f, 0.f});
    }
    const float lo = lightCard ? 0.44f : 0.68f, hi = lightCard ? 0.58f : 0.83f;
    const float L = c.L < lo ? lo : (c.L > hi ? hi : c.L);
    return fromLch(L, C < 0.12f ? 0.12f : C, hue(c));
}

} // namespace osd::colour
