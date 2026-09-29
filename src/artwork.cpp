#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <vector>
#include <mutex>

#include "artwork.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

namespace G = Gdiplus;

namespace osd {
namespace {

ULONG_PTR g_token = 0;
std::mutex g_tokenMutex; // start/stop can race between the prewarm thread and the main thread

// The cover's colour, chosen the way foo_mediabar does it: the most populous 16-level RGB bucket
// (not a mean, which turns two opposite hues into grey), weighting vivid pixels by saturation^2
// and the centre of the picture over its edges. Near-black and near-white pixels are ignored.
// A monochrome cover has no vivid pixels, so a second pass answers with its dominant grey.
// The result is the raw cover colour; the window makes it legible on the card (see
// OsdWindow::resolveColors), which is what turns a grey cover into off-white or charcoal.
void pickAccent(Artwork& art) {
    if (art.w <= 0 || art.h <= 0 || art.px.empty()) return;
    constexpr int kLevels = 16;
    constexpr int kBuckets = kLevels * kLevels * kLevels;
    constexpr float kMinValue = 0.10f, kMaxValue = 0.97f, kMinSat = 0.12f, kCorner = 0.30f, kMinTotal = 8.f;

    const size_t pixels = static_cast<size_t>(art.w) * static_cast<size_t>(art.h);
    const size_t step = pixels / 16384 > 1 ? pixels / 16384 : 1;
    const float halfW = static_cast<float>(art.w) * 0.5f, halfH = static_cast<float>(art.h) * 0.5f;

    std::vector<float> weight(kBuckets, 0.f);
    int best = -1;
    for (int pass = 0; pass < 2 && best < 0; ++pass) {
        const bool chroma = pass == 0;
        if (!chroma) std::fill(weight.begin(), weight.end(), 0.f);
        float total = 0.f;
        for (size_t i = 0; i < pixels; i += step) {
            const std::uint32_t p = art.px[i];
            if ((p >> 24) < 250u) continue; // translucent = border or shadow, not the cover's colour
            const int r = static_cast<int>((p >> 16) & 0xFF), g = static_cast<int>((p >> 8) & 0xFF), b = static_cast<int>(p & 0xFF);
            const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
            const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
            const float value = static_cast<float>(mx) / 255.f;
            if (value < kMinValue || value > kMaxValue) continue;
            const float sat = static_cast<float>(mx - mn) / static_cast<float>(mx);
            if (chroma && sat < kMinSat) continue;
            const float dx = (static_cast<float>(static_cast<size_t>(i) % static_cast<size_t>(art.w)) + 0.5f - halfW) / halfW;
            const float dy = (static_cast<float>(static_cast<size_t>(i) / static_cast<size_t>(art.w)) + 0.5f - halfH) / halfH;
            float radius = std::sqrt(dx * dx + dy * dy) * 0.70710678f;
            if (radius > 1.f) radius = 1.f;
            const float centre = 1.f - (1.f - kCorner) * radius * radius;
            const float w = chroma ? sat * sat * centre : centre;
            weight[static_cast<size_t>(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4))] += w;
            total += w;
        }
        if (chroma && total < kMinTotal) continue; // monochrome: try the grey pass
        if (total <= 0.f) return;                 // nothing opaque at all
        float bestW = 0.f;
        for (int k = 0; k < kBuckets; ++k)
            if (weight[static_cast<size_t>(k)] > bestW) {
                bestW = weight[static_cast<size_t>(k)];
                best = k;
            }
    }
    if (best < 0) return;

    // The real average of the winning bucket, not the centre of a 16-wide step.
    double sr = 0, sg = 0, sb = 0, n = 0;
    for (size_t i = 0; i < pixels; i += step) {
        const std::uint32_t p = art.px[i];
        if ((p >> 24) < 250u) continue;
        const int r = static_cast<int>((p >> 16) & 0xFF), g = static_cast<int>((p >> 8) & 0xFF), b = static_cast<int>(p & 0xFF);
        if (((r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)) != best) continue;
        sr += r;
        sg += g;
        sb += b;
        n += 1.0;
    }
    if (n <= 0.0) return;
    const auto ch = [n](double sum) { return static_cast<std::uint32_t>(sum / n + 0.5); };
    art.accent = (ch(sr) << 16) | (ch(sg) << 8) | ch(sb);
    art.hasAccent = true;
}

} // namespace

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
