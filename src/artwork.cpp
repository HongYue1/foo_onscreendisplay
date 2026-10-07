#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

#include "artwork.h"
#include "fbc/cover_accent.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

namespace G = Gdiplus;

namespace osd {
namespace {

ULONG_PTR g_token = 0;
std::mutex g_tokenMutex; // start/stop can race between the prewarm thread and the main thread


} // namespace

// The cover's colour comes from the shared fb2k-common library (fbc::cover_colours, the same
// pick as Better Tabs, Enhanced Playlist Tabs and Media Bar): the most prominent vivid colour,
// judged against what its hue can reach in sRGB. It reads the PARGB buffer as is, no copy.
// The result is the raw cover colour; colour::accent_for_background makes it legible on the card.
void pickAccent(Artwork& art) {
    art.hasAccent = false;
    art.hasSecondary = false;
    if (art.w <= 0 || art.h <= 0 || art.px.size() != static_cast<size_t>(art.w) * static_cast<size_t>(art.h)) return;
    const auto colours = fbc::cover_colours(reinterpret_cast<const std::uint8_t*>(art.px.data()),
                                            static_cast<std::uint32_t>(art.w), static_cast<std::uint32_t>(art.h));
    if (!colours) return;
    art.accent = colours->primary;
    art.hasAccent = true;
    art.hasSecondary = colours->secondary.has_value();
    art.secondary = colours->secondary.value_or(0u);
    art.colourfulness = colours->colourfulness;
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
