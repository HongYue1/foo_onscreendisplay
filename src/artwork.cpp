#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

#include "artwork.h"
#include "colour.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

namespace G = Gdiplus;

namespace osd {
namespace {

ULONG_PTR g_token = 0;
std::mutex g_tokenMutex; // start/stop can race between the prewarm thread and the main thread

float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

} // namespace

// The cover's colour, in OKLab (see colour.h), in four steps:
//  1. Every sampled pixel gets a weight: the centre of the picture counts more than its edges
//     (borders, logos, barcodes), and near-black / near-white pixels count little.
//  2. Colourful pixels (chroma above grey) are binned by hue, 36 bins of 10 degrees, weighted by
//     how colourful they are and how usable their lightness is.
//  3. Each hue (with its neighbours, so a hue on a bin edge is not split in two) is scored by
//     population^0.7 x vividness. A large area wins, but a vivid area beats a larger dull one,
//     which is what reads as a cover's "colour": the red title on a brown sleeve, not the brown.
//  4. The winner is the weighted OKLab mean of its pixels: a real colour from the cover, not a
//     bucket centre, and averaged in a space where averaging does not turn to mud.
// A cover with almost no colourful pixels answers with its dominant grey. The result is the raw
// cover colour; colour::accentForCard makes it legible on the card.
void pickAccent(Artwork& art) {
    if (art.w <= 0 || art.h <= 0 || art.px.empty()) return;
    constexpr int kHues = 36;
    constexpr int kGreys = 16;
    constexpr float kCorner = 0.30f;    // weight of a corner pixel relative to the centre
    constexpr float kMinColour = 0.02f; // share of colourful weight below which the cover is grey
    constexpr float kPi = 3.14159265f;

    static const auto kLinear = [] {
        std::vector<float> t(256);
        for (int i = 0; i < 256; ++i) t[static_cast<size_t>(i)] = colour::srgbToLinear(static_cast<float>(i) / 255.f);
        return t;
    }();

    struct Bin {
        double w = 0, L = 0, a = 0, b = 0, c = 0;
    };
    Bin hues[kHues];
    Bin greys[kGreys];
    double total = 0, colourful = 0;

    const size_t pixels = static_cast<size_t>(art.w) * static_cast<size_t>(art.h);
    const size_t step = pixels / 16384 > 1 ? pixels / 16384 : 1;
    const float halfW = static_cast<float>(art.w) * 0.5f, halfH = static_cast<float>(art.h) * 0.5f;
    for (size_t i = 0; i < pixels; i += step) {
        const std::uint32_t p = art.px[i];
        if ((p >> 24) < 250u) continue; // translucent = border or shadow, not the cover's colour
        const colour::Lab c = colour::linearToOklab(kLinear[(p >> 16) & 0xFF], kLinear[(p >> 8) & 0xFF], kLinear[p & 0xFF]);
        const float dx = (static_cast<float>(i % static_cast<size_t>(art.w)) + 0.5f - halfW) / halfW;
        const float dy = (static_cast<float>(i / static_cast<size_t>(art.w)) + 0.5f - halfH) / halfH;
        const float radius = (std::min)(1.f, std::sqrt(dx * dx + dy * dy) * 0.70710678f);
        float w = 1.f - (1.f - kCorner) * radius * radius;
        // Near black and near white say little about a cover's colour.
        w *= 0.15f + 0.85f * smoothstep(0.10f, 0.22f, c.L) * (1.f - smoothstep(0.93f, 0.99f, c.L));
        total += w;

        const float C = colour::chroma(c);
        const float vivid = smoothstep(colour::kGreyChroma, 0.12f, C);
        if (vivid > 0.f) {
            colourful += w * vivid;
            // Lightness where an accent can live; very dark or pale colours are only a last resort.
            const float usable = 0.35f + 0.65f * smoothstep(0.22f, 0.40f, c.L) * (1.f - smoothstep(0.88f, 0.97f, c.L));
            const double cw = static_cast<double>(w * vivid * usable);
            float h = colour::hue(c);
            if (h < 0.f) h += 2.f * kPi;
            Bin& bin = hues[(std::min)(kHues - 1, static_cast<int>(h / (2.f * kPi) * kHues))];
            bin.w += cw;
            bin.L += cw * c.L;
            bin.a += cw * c.a;
            bin.b += cw * c.b;
            bin.c += cw * C;
        }
        if (C < 0.06f) {
            Bin& g = greys[(std::min)(kGreys - 1, static_cast<int>(c.L * kGreys))];
            g.w += w;
            g.L += w * c.L;
        }
    }
    if (total <= 0.0) return; // nothing opaque at all

    colour::Lab pick;
    if (colourful >= kMinColour * total) {
        int best = -1;
        double bestScore = 0;
        for (int i = 0; i < kHues; ++i) {
            const Bin& l = hues[(i + kHues - 1) % kHues];
            const Bin& m = hues[i];
            const Bin& r = hues[(i + 1) % kHues];
            const double pop = 0.5 * l.w + m.w + 0.5 * r.w;
            if (pop <= 0.0) continue;
            const double meanC = (0.5 * l.c + m.c + 0.5 * r.c) / pop;
            const double score = std::pow(pop / total, 0.7) * (0.5 + 4.0 * meanC);
            if (score > bestScore) {
                bestScore = score;
                best = i;
            }
        }
        if (best < 0) return;
        Bin sum;
        for (int d = -1; d <= 1; ++d) {
            const Bin& b = hues[(best + d + kHues) % kHues];
            sum.w += b.w;
            sum.L += b.L;
            sum.a += b.a;
            sum.b += b.b;
        }
        pick = colour::Lab{static_cast<float>(sum.L / sum.w), static_cast<float>(sum.a / sum.w), static_cast<float>(sum.b / sum.w)};
    } else {
        // Monochrome: the most common grey level, averaged with its neighbours.
        int best = 0;
        for (int i = 1; i < kGreys; ++i)
            if (greys[i].w > greys[best].w) best = i;
        double w = 0, L = 0;
        for (int d = -1; d <= 1; ++d) {
            const int k = best + d;
            if (k < 0 || k >= kGreys) continue;
            w += greys[k].w;
            L += greys[k].L;
        }
        if (w <= 0.0) return;
        pick = colour::Lab{static_cast<float>(L / w), 0.f, 0.f};
    }
    art.accent = colour::toRgb(pick);
    art.hasAccent = true;
}

bool startGdiplus() {
    std::lock_guard<std::mutex> lock(g_tokenMutex);
    if (g_token != 0) return true;
    G::GdiplusStartupInput in;
    return G::GdiplusStartup(&g_token, &in, nullptr) == G::Ok;
}

void stopGdiplus() {
    std::lock_guard<std::mutex> lock(g_tokenMutex);
    if (g_token != 0) {
        G::GdiplusShutdown(g_token);
        g_token = 0;
    }
}

void prewarmFonts() {
    if (!startGdiplus()) return;
    // The first FontFamily makes GDI+ build its process-wide font list: about 300 ms. Paying it
    // here, off the UI thread, means the first card does not stall.
    G::FontFamily family(L"Segoe UI");
    (void)family;
}

std::shared_ptr<const Artwork> decodeArtwork(const void* data, std::size_t size) {
    if (g_token == 0 || data == nullptr || size == 0) return nullptr;

    // SHCreateMemStream copies the bytes. GDI+ needs the stream to outlive the bitmap.
    IStream* stream = SHCreateMemStream(static_cast<const BYTE*>(data), static_cast<UINT>(size));
    if (stream == nullptr) return nullptr;

    std::shared_ptr<Artwork> art;
    {
        G::Bitmap src(stream);
        const int w = static_cast<int>(src.GetWidth());
        const int h = static_cast<int>(src.GetHeight());
        if (src.GetLastStatus() == G::Ok && w > 0 && h > 0) {
            const int side = w < h ? w : h;
            const int sx = (w - side) / 2;
            const int sy = (h - side) / 2;
            constexpr int N = Artwork::kSide;

            art = std::make_shared<Artwork>();
            art->w = N;
            art->h = N;
            art->px.assign(static_cast<size_t>(N) * N, 0u);
            {
                G::Bitmap dst(N, N, N * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(art->px.data()));
                G::Graphics g(&dst);
                g.SetInterpolationMode(G::InterpolationModeHighQualityBicubic);
                g.SetPixelOffsetMode(G::PixelOffsetModeHalf);
                g.Clear(G::Color(255, 0, 0, 0));
                G::ImageAttributes ia;
                ia.SetWrapMode(G::WrapModeTileFlipXY);
                g.DrawImage(&src, G::Rect(0, 0, N, N), sx, sy, side, side, G::UnitPixel, &ia);
            }
            pickAccent(*art);
        }
    }
    stream->Release();
    return art;
}

} // namespace osd
