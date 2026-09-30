#pragma once

// The OSD card itself: a click-through, always-on-top, per-pixel-alpha window drawn with GDI+.
// It knows nothing about foobar2000. The controller hands it a Content and a Settings and it
// animates in, holds, and animates out. Everything here runs on the main thread.
//
// Cost model (see the README): nothing runs while hidden; a full repaint happens only when the
// card is shown or its content changes; during the hold only the progress row is repainted, and
// only when a pixel or a second changed; during fades only the layered window's alpha/position
// is updated, with no repaint at all. The hold timer is re-armed for the next moment something
// can change (a second ticking over, the bar gaining a pixel, the hold ending), not polled.

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "text_engine.h"

namespace Gdiplus {
class Bitmap;
class Graphics;
} // namespace Gdiplus

namespace osd {

// Cover art, already square-cropped and scaled to kSide, premultiplied BGRA. Produced on a
// worker thread (see artwork.cpp) and shared read-only afterwards.
struct Artwork {
    static constexpr int kSide = 256;
    int w = 0;
    int h = 0;
    std::vector<std::uint32_t> px;
    bool hasAccent = false;
    std::uint32_t accent = 0; // 0xRRGGBB
};

struct Content {
    std::wstring line1;
    std::wstring line2;
    std::wstring line3;
    std::shared_ptr<const Artwork> art;
    bool paused = false;
    double length = 0.0; // seconds; 0 means unknown (a stream)
};

class OsdWindow {
public:
    OsdWindow();
    ~OsdWindow();
    OsdWindow(const OsdWindow&) = delete;
    OsdWindow& operator=(const OsdWindow&) = delete;

    bool create();
    void destroy();

    // Where the progress bar gets its playback position from, in seconds.
    void setPositionSource(std::function<double()> fn) { m_position = std::move(fn); }

    // Show (or refresh and extend) the card. anchor is foobar2000's main window: with
    // MonitorMain it decides the monitor. The DPI is always the chosen monitor's.
    void show(const Settings& settings, const Content& content, HWND anchor);
    // Replace what a visible card displays (new artwork, text, pause state) without restarting it.
    void setContent(const Content& content);
    // Start fading out from wherever the card is now.
    void hide();

    bool visible() const { return m_state != State::Hidden; }

    // Test hook: lay out and draw one complete frame with no window involved. Returns the
    // premultiplied BGRA pixels, valid until the next call.
    const std::uint32_t* debugRender(const Settings& settings, const Content& content, double position, int& w, int& h);

private:
    enum class State { Hidden, FadeIn, Hold, FadeOut };

    struct Colors {
        std::uint32_t accent = 0;   // resolved accent
        std::uint32_t bg = 0;
        std::uint32_t text = 0;
        std::uint32_t fg = 0;       // bar / glyph colour
        bool lightBg = false;
    };

    struct Layout {
        float scale = 1.f;
        int shadow = 0;
        int cardW = 0, cardH = 0;
        int winW = 0, winH = 0;
        float radius = 0.f;
        float cardX = 0, cardY = 0;
        float artX = 0, artY = 0, artSize = 0, artRadius = 0;
        float textX = 0, textW = 0;
        float row1Y = 0, row2Y = 0, row3Y = 0;
        float rowH1 = 0, rowH2 = 0, rowH3 = 0;
        float fTitle = 0, fLine2 = 0, fLine3 = 0, fTime = 0;
        float glyphX = 0, glyphY = 0, glyphSize = 0;
        float barRowY = 0, barRowH = 0;
        float regionX = 0, regionW = 0; // horizontal span of the progress row
        float timeW = 0;
        float barX = 0, barW = 0, barH = 0;
        bool hasRow1 = false, hasRow2 = false, hasRow3 = false, hasBarRow = false;
    };

    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    void beginContent(const Settings& s, const Content& c, HWND anchor);
    void placeOnMonitor();
    void onDisplayChanged();
    void updateHover(float elapsedMs);
    void scheduleTimer(ULONGLONG now);
    void prepare();
    void resolveColors();
    void computeLayout();
    void initFaces();
    void ensureBuffers();
    void buildShadow();
    void renderStatic();
    void drawProgress(double pos);
    Gdiplus::Bitmap* scaledArt(int side);
    void present(bool full);
    void setTimer(UINT ms);
    void stopTimer();
    void onTimer();
    void computePlacement(int& x, int& y) const;

    HWND m_hwnd = nullptr;
    HDC m_memDc = nullptr;
    HBITMAP m_dib = nullptr;
    HGDIOBJ m_oldBitmap = nullptr;
    void* m_bits = nullptr;
    int m_bufW = 0;
    int m_bufH = 0;
    std::vector<std::uint32_t> m_static;
    std::vector<std::uint32_t> m_shadowPx;
    int m_shadowKey[6] = {-1, -1, -1, -1, -1, -1};
    std::unique_ptr<Gdiplus::Bitmap> m_staticBmp;
    std::unique_ptr<Gdiplus::Bitmap> m_frameBmp;
    std::unique_ptr<Gdiplus::Graphics> m_staticG;
    std::unique_ptr<Gdiplus::Graphics> m_frameG;

    // Scaled copy of the cover at the size it is drawn, so a repaint does not rescale it.
    std::shared_ptr<const Artwork> m_artKeep;
    std::unique_ptr<Gdiplus::Bitmap> m_artScaled;
    int m_artSide = 0;

    Settings m_s;
    Content m_c;
    Layout m_l;
    Colors m_col;
    RECT m_work{};
    HWND m_anchor = nullptr;
    text::Faces m_faces[4]; // title, line 2, line 3, times

    State m_state = State::Hidden;
    ULONGLONG m_stateStart = 0;
    ULONGLONG m_lastTick = 0;
    float m_alpha = 0.f;     // animation alpha, 0-1
    float m_slide = 0.f;     // animation offset, 0 = in place
    float m_fadeFrom = 0.f;  // alpha when the current fade started, so a fade reversed midway does not jump
    float m_slideFrom = 0.f; // slide when the current fade started
    float m_hover = 1.f;     // multiplier while the mouse pointer is over the card
    float m_hoverTarget = 1.f;
    UINT m_timerMs = 0;
    long long m_lastKey = -1;
    BYTE m_lastAlpha = 0;
    POINT m_lastPos{-32000, -32000};

    std::function<double()> m_position;
};

} // namespace osd
