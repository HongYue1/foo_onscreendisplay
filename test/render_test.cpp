// Offline render test: draws every preset through OsdWindow::debugRender (no foobar2000), checks the
// frames are sane, times them, and writes contact sheets as JPEG for a human to look at.
//   render_test.exe [outdir]
// Build with test/build_render_test.bat.

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/artwork.h"
#include "../src/config.h"
#include "../src/osd_window.h"
#include "../src/presets.h"

namespace G = Gdiplus;
using namespace osd;

namespace {

double nowMs() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return 1000.0 * static_cast<double>(c.QuadPart) / static_cast<double>(f.QuadPart);
}

std::shared_ptr<const Artwork> makeArt() {
    auto a = std::make_shared<Artwork>();
    a->w = a->h = Artwork::kSide;
    a->px.resize(static_cast<size_t>(a->w) * a->h);
    for (int y = 0; y < a->h; ++y) {
        for (int x = 0; x < a->w; ++x) {
            const int r = 200 - y * 120 / a->h;
            const int g = 60 + x * 100 / a->w;
            const int b = 120 + ((x / 32 + y / 32) % 2) * 60;
            a->px[static_cast<size_t>(y) * a->w + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
    a->hasAccent = true;
    a->accent = 0xE0507A;
    return a;
}

bool jpegClsid(CLSID* out) {
    UINT n = 0, size = 0;
    G::GetImageEncodersSize(&n, &size);
    std::vector<char> buf(size);
    auto* info = reinterpret_cast<G::ImageCodecInfo*>(buf.data());
    G::GetImageEncoders(n, size, info);
    for (UINT i = 0; i < n; ++i) {
        if (wcscmp(info[i].MimeType, L"image/jpeg") == 0) {
            *out = info[i].Clsid;
            return true;
        }
    }
    return false;
}

void saveJpeg(G::Bitmap& bmp, const std::wstring& path, ULONG quality) {
    CLSID clsid{};
    if (!jpegClsid(&clsid)) return;
    G::EncoderParameters ep{};
    ep.Count = 1;
    ep.Parameter[0].Guid = G::EncoderQuality;
    ep.Parameter[0].Type = G::EncoderParameterValueTypeLong;
    ep.Parameter[0].NumberOfValues = 1;
    ep.Parameter[0].Value = &quality;
    bmp.Save(path.c_str(), &clsid, &ep);
}

struct Frame {
    std::vector<std::uint32_t> px;
    int w = 0, h = 0;
};

int findPreset(const char* name) {
    for (int i = 0; i < presets::count(); ++i)
        if (std::strncmp(presets::name(i), name, std::strlen(name)) == 0) return i;
    return 0;
}

// Draws frames on a mid-grey checker-free backdrop (so translucency shows) in a grid.
void sheet(const std::vector<Frame>& frames, int cols, const std::wstring& path) {
    int cw = 0, ch = 0;
    for (const Frame& f : frames) {
        cw = f.w > cw ? f.w : cw;
        ch = f.h > ch ? f.h : ch;
    }
    const int rows = (static_cast<int>(frames.size()) + cols - 1) / cols;
    G::Bitmap out(cw * cols, ch * rows, PixelFormat24bppRGB);
    G::Graphics g(&out);
    G::LinearGradientBrush bg(G::Point(0, 0), G::Point(cw * cols, ch * rows), G::Color(255, 96, 120, 150), G::Color(255, 190, 170, 130));
    g.FillRectangle(&bg, 0, 0, cw * cols, ch * rows);
    for (size_t i = 0; i < frames.size(); ++i) {
        const Frame& f = frames[i];
        G::Bitmap b(f.w, f.h, f.w * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(f.px.data())));
        g.DrawImage(&b, static_cast<int>(i % cols) * cw, static_cast<int>(i / cols) * ch, f.w, f.h);
    }
    saveJpeg(out, path, 72);
}

// Compact review sheet: frames trimmed to their visible bounds, packed in two columns on a flat
// backdrop, low JPEG quality so it stays small enough to move around as text.
Frame trim(const Frame& f) {
    int x0 = f.w, y0 = f.h, x1 = -1, y1 = -1;
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x)
            if ((f.px[static_cast<size_t>(y) * f.w + x] >> 24) > 24) {
                x0 = x < x0 ? x : x0;
                y0 = y < y0 ? y : y0;
                x1 = x > x1 ? x : x1;
                y1 = y > y1 ? y : y1;
            }
    Frame out;
    if (x1 < 0) return out;
    out.w = x1 - x0 + 1;
    out.h = y1 - y0 + 1;
    out.px.resize(static_cast<size_t>(out.w) * out.h);
    for (int y = 0; y < out.h; ++y)
        std::memcpy(&out.px[static_cast<size_t>(y) * out.w], &f.px[static_cast<size_t>(y + y0) * f.w + x0], static_cast<size_t>(out.w) * 4);
    return out;
}

void review(const std::vector<Frame>& left, const std::vector<Frame>& right, const std::wstring& path, ULONG quality) {
    int lw = 0, lh = 0, rw = 0, rh = 0;
    for (const Frame& f : left) {
        lw = f.w > lw ? f.w : lw;
        lh += f.h + 8;
    }
    for (const Frame& f : right) {
        rw = f.w > rw ? f.w : rw;
        rh += f.h + 8;
    }
    const int W = lw + rw + 24, H = (lh > rh ? lh : rh) + 8;
    G::Bitmap out(W, H, PixelFormat24bppRGB);
    G::Graphics g(&out);
    g.Clear(G::Color(255, 72, 82, 98));
    auto put = [&](const std::vector<Frame>& col, int x) {
        int y = 8;
        for (const Frame& f : col) {
            G::Bitmap b(f.w, f.h, f.w * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(f.px.data())));
            g.DrawImage(&b, x, y, f.w, f.h);
            y += f.h + 8;
        }
    };
    put(left, 8);
    put(right, lw + 16);
    saveJpeg(out, path, quality);
    std::printf("review sheet %dx%d\n", W, H);
}

// ASCII dump of a frame over black: block-averaged brightness, cells 3 px wide by 6 px tall.
void ascii(const char* title, const Frame& f) {
    static const char ramp[] = " .:-=+*#%@";
    std::printf("--- %s (%dx%d)\n", title, f.w, f.h);
    for (int y0 = 0; y0 < f.h; y0 += 6) {
        std::string line;
        for (int x0 = 0; x0 < f.w; x0 += 3) {
            double sum = 0;
            int n = 0;
            for (int y = y0; y < y0 + 6 && y < f.h; ++y)
                for (int x = x0; x < x0 + 3 && x < f.w; ++x) {
                    const std::uint32_t p = f.px[static_cast<size_t>(y) * f.w + x];
                    // premultiplied: colour channels are already scaled by alpha, so this is brightness over black
                    sum += (0.2126 * ((p >> 16) & 255) + 0.7152 * ((p >> 8) & 255) + 0.0722 * (p & 255)) / 255.0;
                    ++n;
                }
            const int idx = static_cast<int>(sum / n * 9.99);
            line.push_back(ramp[idx < 0 ? 0 : (idx > 9 ? 9 : idx)]);
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        std::printf("%s\n", line.c_str());
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const std::wstring dir = argc > 1 ? argv[1] : L"out";
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!startGdiplus()) {
        std::printf("GDI+ failed\n");
        return 2;
    }
    int failures = 0;
    {
        const double f0 = nowMs();
        {
            G::FontFamily fam(L"Segoe UI");
            (void)fam;
        }
        std::printf("first FontFamily(Segoe UI): %.1f ms\n", nowMs() - f0);
        OsdWindow win;
        if (!win.create()) {
            std::printf("create failed\n");
            return 2;
        }

        Content latin;
        latin.line1 = L"Bohemian Rhapsody";
        latin.line2 = L"Queen";
        latin.line3 = L"A Night at the Opera (1975)";
        latin.length = 355;
        latin.art = makeArt();

        Content mixed = latin;
        mixed.line1 = L"\u591C\u306B\u99C6\u3051\u308B \u2014 YOASOBI \u266A \U0001F600 \u0645\u0631\u062D\u0628\u0627";
        mixed.line2 = L"\u041F\u0440\u0438\u0432\u0435\u0442 \u00DCn\u00EFc\u00F6d\u00E9 \uC548\uB155\uD558\uC138\uC694";
        mixed.line3 = L"A very long album title that certainly cannot fit on one line of this card, (2026)";

        // ---- every preset: sanity + timing --------------------------------------------------
        std::printf("%-22s %9s %9s %9s  %s\n", "preset", "size", "first ms", "warm ms", "check");
        std::vector<Frame> all;
        for (int i = 0; i < presets::count(); ++i) {
            Settings s;
            presets::apply(i, s);
            int w = 0, h = 0;
            const double t0 = nowMs();
            const std::uint32_t* px = win.debugRender(s, latin, 142.0, w, h);
            const double t1 = nowMs();
            if (!px) {
                std::printf("%-22s render returned null\n", presets::name(i));
                ++failures;
                continue;
            }
            Frame f;
            f.w = w;
            f.h = h;
            f.px.assign(px, px + static_cast<size_t>(w) * h);
            // Sanity: something opaque in the middle, nothing at the very edge (shadow not clipped).
            size_t opaque = 0, edge = 0;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const std::uint32_t a = f.px[static_cast<size_t>(y) * w + x] >> 24;
                    if (a > 200) ++opaque;
                    if ((x == 0 || y == 0 || x == w - 1 || y == h - 1) && a > 8) ++edge;
                }
            const double t2 = nowMs();
            for (int k = 0; k < 20; ++k) win.debugRender(s, latin, 142.0 + k, w, h);
            const double warm = (nowMs() - t2) / 20.0;
            const char* verdict = opaque < static_cast<size_t>(w) * h / 20 ? "EMPTY?" : (edge > 0 ? "edge clipped" : "ok");
            if (verdict[0] != 'o') ++failures;
            std::printf("%-22s %4dx%-4d %9.2f %9.2f  %s\n", presets::name(i), w, h, t1 - t0, warm, verdict);
            if (i == 2 || i == 5) { // Snow (shadow on), AMOLED (shadow off): alpha going up from the bottom edge
                std::printf("  bottom alpha profile %s:", presets::name(i));
                for (int y = h - 1; y >= h - 32 && y >= 0; --y) std::printf(" %u", f.px[static_cast<size_t>(y) * w + w / 2] >> 24);
                std::printf("\n");
            }
            all.push_back(std::move(f));
        }

        // ---- picked sizes are exact: 12 pt title / 10 pt detail is the Classic layout's own size ----
        for (int pt : {0, 120, 240}) {
            Settings s;
            s.titlePt = pt;
            s.detailPt = pt ? pt * 10 / 12 : 0;
            int w = 0, h = 0;
            win.debugRender(s, latin, 10.0, w, h);
            std::printf("title %2d pt, detail %2d pt -> card window %dx%d\n", pt / 10, s.detailPt / 10, w, h);
        }

        // ---- progress-only repaint cost (what runs every 250 ms during the hold) --------------
        {
            Settings s;
            int w = 0, h = 0;
            win.debugRender(s, latin, 10.0, w, h);
            const double t = nowMs();
            for (int k = 0; k < 200; ++k) win.debugRender(s, latin, 10.0 + k, w, h);
            std::printf("default preset full frame, avg of 200: %.2f ms\n", (nowMs() - t) / 200.0);
        }

        // ---- sheets ---------------------------------------------------------------------------
        {
            std::vector<Frame> layouts;
            const char* names[] = {"Midnight", "Compact", "Banner", "Poster"};
            for (const char* n : names) {
                Settings s;
                presets::apply(findPreset(n), s);
                Frame f;
                const std::uint32_t* px = win.debugRender(s, latin, 142.0, f.w, f.h);
                if (px) f.px.assign(px, px + static_cast<size_t>(f.w) * f.h);
                layouts.push_back(std::move(f));
            }
            sheet(layouts, 2, dir + L"\\layouts.jpg");

            std::vector<Frame> looks;
            const char* looksNames[] = {"Snow", "Cover colour", "Neon", "Retro", "Sunset", "Paper"};
            for (const char* n : looksNames) {
                Settings s;
                presets::apply(findPreset(n), s);
                s.layout = LayoutClassic;
                Frame f;
                const std::uint32_t* px = win.debugRender(s, latin, 142.0, f.w, f.h);
                if (px) f.px.assign(px, px + static_cast<size_t>(f.w) * f.h);
                looks.push_back(std::move(f));
            }
            sheet(looks, 2, dir + L"\\looks.jpg");

            std::vector<Frame> fb;
            for (int lay = 0; lay < LayoutCount; ++lay) {
                Settings s;
                s.layout = lay;
                Frame f;
                const std::uint32_t* px = win.debugRender(s, mixed, 60.0, f.w, f.h);
                if (px) f.px.assign(px, px + static_cast<size_t>(f.w) * f.h);
                fb.push_back(std::move(f));
            }
            sheet(fb, 2, dir + L"\\fallback.jpg");
        }
        {
            auto one = [&](const char* name, const Content& c, int layout) {
                Settings s;
                if (name) presets::apply(findPreset(name), s);
                if (layout >= 0) s.layout = layout;
                Frame f;
                const std::uint32_t* px = win.debugRender(s, c, 142.0, f.w, f.h);
                if (px) f.px.assign(px, px + static_cast<size_t>(f.w) * f.h);
                return trim(f);
            };
            std::vector<Frame> left{one("Midnight", mixed, -1), one("Compact", latin, -1), one("Banner", latin, -1)};
            std::vector<Frame> right{one("Poster", latin, -1)};
            review(left, right, dir + L"\\review.jpg", 20);
            ascii("Midnight, latin", one("Midnight", latin, -1));
            ascii("Midnight, mixed scripts", left[0]);
            ascii("Compact", left[1]);
            ascii("Poster", right[0]);
        }
        win.destroy();
    }
    text::shutdown();
    stopGdiplus();
    std::printf("failures: %d\n", failures);
    return failures ? 1 : 0;
}
