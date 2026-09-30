#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "osd_window.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace G = Gdiplus;

namespace osd {
namespace {

constexpr wchar_t kClassName[] = L"foo_osd_window";
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kAnimTimerMs = 16;     // fades: alpha/position only, no repaint
constexpr double kIdlePollMs = 1000;  // hold with nothing that can change (paused and kept up)
constexpr double kHoverPollMs = 100;  // hold while "fade under the mouse pointer" is on
constexpr float kHoverAlpha = 0.18f;  // how much of the card is left under the pointer
constexpr float kHoverFadeMs = 140.f; // time for a full hover fade

// The monitor's effective DPI. GetDpiForMonitor (Windows 8.1) is resolved at run time so the DLL
// still loads on Windows 7, which the SDK targets; there the system DPI is the right answer anyway.
UINT monitorDpi(HMONITOR mon) {
    using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static const Fn getDpiForMonitor = [] {
        HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return shcore ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(shcore, "GetDpiForMonitor"))) : nullptr;
    }();
    UINT x = 0, y = 0;
    if (getDpiForMonitor && mon && SUCCEEDED(getDpiForMonitor(mon, 0 /* MDT_EFFECTIVE_DPI */, &x, &y)) && x != 0) return x;
    HDC screen = GetDC(nullptr);
    const int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen) ReleaseDC(nullptr, screen);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96u;
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float minf(float a, float b) { return a < b ? a : b; }
float maxf(float a, float b) { return a > b ? a : b; }
int roundi(float v) { return static_cast<int>(std::lround(v)); }

G::Color argb(int a, std::uint32_t rgb) {
    return G::Color(static_cast<BYTE>(a), static_cast<BYTE>((rgb >> 16) & 0xFF), static_cast<BYTE>((rgb >> 8) & 0xFF),
                    static_cast<BYTE>(rgb & 0xFF));
}

std::uint32_t mixRgb(std::uint32_t a, std::uint32_t b, float t) {
    auto ch = [&](int shift) {
        const float ca = static_cast<float>((a >> shift) & 0xFF);
        const float cb = static_cast<float>((b >> shift) & 0xFF);
        return static_cast<std::uint32_t>(roundi(ca + (cb - ca) * t)) & 0xFF;
    };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

float luminance(std::uint32_t c) {
    return (0.2126f * static_cast<float>((c >> 16) & 0xFF) + 0.7152f * static_cast<float>((c >> 8) & 0xFF) +
            0.0722f * static_cast<float>(c & 0xFF)) /
           255.f;
}

// Makes a colour taken from a cover usable as the accent on a dark or light card, the way
// foo_mediabar does: hue kept, saturation floored and lightness windowed so it is never a muddy
// smudge; a monochrome cover has no hue, so it becomes legible off-white (dark card) or charcoal.
std::uint32_t accentForCard(std::uint32_t rgb, bool lightCard) {
    const float r = static_cast<float>((rgb >> 16) & 0xFF) / 255.f, g = static_cast<float>((rgb >> 8) & 0xFF) / 255.f,
                b = static_cast<float>(rgb & 0xFF) / 255.f;
    const float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float l = (mx + mn) * 0.5f, s = 0.f, h = 0.f;
    const float d = mx - mn;
    if (d > 1e-6f) {
        s = d / (1.f - std::fabs(2.f * l - 1.f));
        if (mx == r) h = std::fmod((g - b) / d, 6.f);
        else if (mx == g) h = (b - r) / d + 2.f;
        else h = (r - g) / d + 4.f;
        if (h < 0.f) h += 6.f;
    }
    if (s < 0.12f) {
        s = 0.f;
        l = lightCard ? (l < 0.22f ? l : 0.22f) : (l > 0.82f ? l : 0.82f);
    } else {
        if (s < 0.62f) s = 0.62f;
        const float lo = lightCard ? 0.28f : 0.46f, hi = lightCard ? 0.42f : 0.62f;
        l = l < lo ? lo : (l > hi ? hi : l);
    }
    const float c = (1.f - std::fabs(2.f * l - 1.f)) * s;
    const float x = c * (1.f - std::fabs(std::fmod(h, 2.f) - 1.f));
    const float m = l - c * 0.5f;
    float pr = 0.f, pg = 0.f, pb = 0.f;
    switch (static_cast<int>(h)) {
    case 0: pr = c; pg = x; break;
    case 1: pr = x; pg = c; break;
    case 2: pg = c; pb = x; break;
    case 3: pg = x; pb = c; break;
    case 4: pr = x; pb = c; break;
    default: pr = c; pb = x; break;
    }
    auto ch = [m](float v) { return static_cast<std::uint32_t>(roundi(clampf((v + m) * 255.f, 0.f, 255.f))) & 0xFF; };
    return (ch(pr) << 16) | (ch(pg) << 8) | ch(pb);
}

void roundedPath(G::GraphicsPath& p, const G::RectF& r, float radius) {
    float d = radius * 2.f;
    if (d > r.Width) d = r.Width;
    if (d > r.Height) d = r.Height;
    if (d < 1.f) {
        p.AddRectangle(r);
        return;
    }
    p.AddArc(r.X, r.Y, d, d, 180.f, 90.f);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270.f, 90.f);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.f, 90.f);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90.f, 90.f);
    p.CloseFigure();
}

std::wstring formatTime(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    const long long t = static_cast<long long>(seconds);
    const long long h = t / 3600, m = (t / 60) % 60, s = t % 60;
    wchar_t buf[40];
    if (h > 0)
        swprintf(buf, 40, L"%lld:%02lld:%02lld", h, m, s);
    else
        swprintf(buf, 40, L"%lld:%02lld", m, s);
    return buf;
}

float easeOutCubic(float t) {
    const float u = 1.f - t;
    return 1.f - u * u * u;
}
float easeInCubic(float t) { return t * t * t; }

void setQuality(G::Graphics& g) {
    g.SetSmoothingMode(G::SmoothingModeAntiAlias);
    g.SetInterpolationMode(G::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(G::PixelOffsetModeHalf);
    g.SetTextRenderingHint(G::TextRenderingHintAntiAlias);
}

} // namespace

OsdWindow::OsdWindow() = default;

OsdWindow::~OsdWindow() { destroy(); }

bool OsdWindow::create() {
    if (m_hwnd) return true;
    HINSTANCE inst = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&OsdWindow::wndProc), &inst);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &OsdWindow::wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             kClassName, L"foo_osd", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;

    HDC screen = GetDC(nullptr);
    m_memDc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    return m_memDc != nullptr;
}

void OsdWindow::destroy() {
    stopTimer();
    m_state = State::Hidden;
    m_staticG.reset();
    m_frameG.reset();
    m_frameBmp.reset();
    m_staticBmp.reset();
    m_artScaled.reset();
    m_artKeep.reset();
    m_static.clear();
    m_static.shrink_to_fit();
    m_shadowPx.clear();
    m_shadowPx.shrink_to_fit();
    if (m_dib) {
        SelectObject(m_memDc, m_oldBitmap);
        DeleteObject(m_dib);
        m_dib = nullptr;
        m_bits = nullptr;
    }
    if (m_memDc) {
        DeleteDC(m_memDc);
        m_memDc = nullptr;
    }
    if (m_hwnd) {
        HWND w = m_hwnd;
        m_hwnd = nullptr;
        DestroyWindow(w);
    }
    m_bufW = m_bufH = 0;
}

void OsdWindow::setTimer(UINT ms) {
    if (!m_hwnd || m_timerMs == ms) return;
    SetTimer(m_hwnd, kTimerId, ms, nullptr);
    m_timerMs = ms;
}

void OsdWindow::stopTimer() {
    if (m_hwnd) KillTimer(m_hwnd, kTimerId);
    m_timerMs = 0;
}

LRESULT CALLBACK OsdWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCREATE: {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        break;
    }
    case WM_TIMER:
        if (wp == kTimerId) {
            if (auto* self = reinterpret_cast<OsdWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) self->onTimer();
            return 0;
        }
        break;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        // Monitors or the taskbar moved: keep a visible card inside the work area.
        if (msg == WM_DISPLAYCHANGE || wp == SPI_SETWORKAREA) {
            if (auto* self = reinterpret_cast<OsdWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) self->onDisplayChanged();
        }
        break;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- Public entry points -----------------------------------------------------------------

void OsdWindow::beginContent(const Settings& settings, const Content& content, HWND anchor) {
    m_s = settings;
    m_s.clamp();
    m_c = content;
    m_anchor = anchor;
    placeOnMonitor();
}

// Picks the monitor, its work area and the scale (Size times the monitor's DPI).
void OsdWindow::placeOnMonitor() {
    HMONITOR mon = nullptr;
    if (m_s.monitor == MonitorCursor) {
        POINT pt{};
        if (GetCursorPos(&pt)) mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    } else if (m_s.monitor == MonitorMain && m_anchor != nullptr && IsWindow(m_anchor)) {
        mon = MonitorFromWindow(m_anchor, MONITOR_DEFAULTTONEAREST);
    }
    if (mon == nullptr) mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi))
        m_work = mi.rcWork;
    else
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &m_work, 0);
    m_l.scale = (static_cast<float>(m_s.scale) / 100.f) * (static_cast<float>(monitorDpi(mon)) / 96.f);
}

void OsdWindow::onDisplayChanged() {
    if (!m_hwnd || m_state == State::Hidden) return;
    placeOnMonitor();
    prepare();
    renderStatic();
    present(true);
}

void OsdWindow::show(const Settings& settings, const Content& content, HWND anchor) {
    if (!m_hwnd) return;
    beginContent(settings, content, anchor);
    prepare();
    renderStatic();
    m_lastKey = -1;

    const ULONGLONG now = GetTickCount64();
    const bool wasHidden = m_state == State::Hidden;
    switch (m_state) {
    case State::Hidden:
        m_state = State::FadeIn;
        m_stateStart = now;
        m_alpha = m_fadeFrom = 0.f;
        m_slide = m_slideFrom = 1.f;
        m_hover = m_hoverTarget = 1.f;
        m_lastPos = POINT{-32000, -32000};
        break;
    case State::FadeIn:
        break; // already on its way in
    case State::Hold:
        m_stateStart = now; // extend
        break;
    case State::FadeOut:
        // Turn round from where it is instead of jumping back to fully visible.
        m_state = State::FadeIn;
        m_stateStart = now;
        m_fadeFrom = m_alpha;
        m_slideFrom = m_slide;
        break;
    }
    m_lastTick = now;
    present(true);
    // Every show re-asserts topmost: another topmost window may have been raised since the last one.
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | (wasHidden ? SWP_SHOWWINDOW : 0));
    scheduleTimer(now);
}

void OsdWindow::setContent(const Content& content) {
    if (!m_hwnd || m_state == State::Hidden) return;
    const ULONGLONG now = GetTickCount64();
    // A card kept up by a pause starts its normal countdown once playback resumes.
    if (m_state == State::Hold && m_s.holdWhilePaused && m_c.paused && !content.paused) m_stateStart = now;
    m_c = content;
    prepare();
    renderStatic();
    present(true);
    scheduleTimer(now);
}

void OsdWindow::hide() {
    if (m_state == State::Hidden || m_state == State::FadeOut) return;
    m_state = State::FadeOut;
    m_stateStart = m_lastTick = GetTickCount64();
    m_fadeFrom = m_alpha;
    m_slideFrom = m_slide;
    setTimer(kAnimTimerMs);
}

const std::uint32_t* OsdWindow::debugRender(const Settings& settings, const Content& content, double position, int& w, int& h) {
    beginContent(settings, content, nullptr);
    prepare();
    renderStatic();
    if (!m_bits) return nullptr;
    std::memcpy(m_bits, m_static.data(), m_static.size() * sizeof(std::uint32_t));
    drawProgress(position);
    m_frameG->Flush(G::FlushIntentionSync);
    w = m_bufW;
    h = m_bufH;
    return static_cast<const std::uint32_t*>(m_bits);
}

void OsdWindow::onTimer() {
    const ULONGLONG now = GetTickCount64();
    const float sinceLast = static_cast<float>(now - m_lastTick);
    m_lastTick = now;
    const double elapsed = static_cast<double>(now - m_stateStart);
    const double speed = static_cast<double>(m_s.animSpeed) / 100.0;
    // A fade that starts part-way (reversed midway) takes the matching part of the time.
    const double fadeIn = m_s.animation == AnimNone ? 0.0 : 220.0 / speed * (1.0 - m_fadeFrom);
    const double fadeOut = m_s.animation == AnimNone ? 0.0 : 420.0 / speed;

    switch (m_state) {
    case State::FadeIn: {
        const float t = fadeIn > 0.0 ? static_cast<float>(elapsed / fadeIn) : 1.f;
        if (t >= 1.f) {
            m_state = State::Hold;
            m_stateStart = now;
            m_alpha = 1.f;
            m_slide = 0.f;
        } else {
            const float e = easeOutCubic(t);
            m_alpha = m_fadeFrom + (1.f - m_fadeFrom) * e;
            m_slide = m_slideFrom * (1.f - e);
        }
        break;
    }
    case State::Hold:
        m_alpha = 1.f;
        m_slide = 0.f;
        if (!(m_s.holdWhilePaused && m_c.paused) && elapsed >= static_cast<double>(m_s.seconds) * 1000.0) {
            m_state = State::FadeOut;
            m_stateStart = now;
            m_fadeFrom = 1.f;
            m_slideFrom = 0.f;
        }
        break;
    case State::FadeOut: {
        const float t = fadeOut > 0.0 ? static_cast<float>(elapsed / fadeOut) : 1.f;
        if (t >= 1.f) {
            m_alpha = 0.f;
            m_state = State::Hidden;
            stopTimer();
            ShowWindow(m_hwnd, SW_HIDE);
            return;
        }
        const float e = easeInCubic(t);
        m_alpha = m_fadeFrom * (1.f - e);
        m_slide = m_slideFrom + (0.5f - m_slideFrom) * e;
        break;
    }
    case State::Hidden:
        stopTimer();
        return;
    }
    updateHover(sinceLast);
    present(false);
    scheduleTimer(now);
}

// "Fade under the mouse pointer": the card is click-through, so it must not sit over what the
// user is trying to read or click. Eases towards kHoverAlpha while the pointer is over the card.
void OsdWindow::updateHover(float elapsedMs) {
    m_hoverTarget = 1.f;
    if (m_s.fadeOnHover && m_state != State::Hidden && m_lastPos.x != -32000) {
        POINT pt{};
        const RECT card{m_lastPos.x + m_l.shadow, m_lastPos.y + m_l.shadow, m_lastPos.x + m_l.shadow + m_l.cardW,
                        m_lastPos.y + m_l.shadow + m_l.cardH};
        if (GetCursorPos(&pt) && PtInRect(&card, pt)) m_hoverTarget = kHoverAlpha;
    }
    const float step = clampf(elapsedMs, 0.f, 100.f) / kHoverFadeMs;
    m_hover = m_hover < m_hoverTarget ? minf(m_hoverTarget, m_hover + step) : maxf(m_hoverTarget, m_hover - step);
}

// Fades run at frame rate. The hold only wakes when something visible can change: the elapsed
// time ticking over a second, the bar gaining a (half) pixel, the hold ending, or the hover poll.
void OsdWindow::scheduleTimer(ULONGLONG now) {
    if (!m_hwnd || m_state == State::Hidden) {
        stopTimer();
        return;
    }
    if (m_state != State::Hold || m_hover != m_hoverTarget) {
        setTimer(kAnimTimerMs);
        return;
    }
    double next = kIdlePollMs;
    if (!(m_s.holdWhilePaused && m_c.paused)) {
        const double left = static_cast<double>(m_s.seconds) * 1000.0 - static_cast<double>(now - m_stateStart);
        next = (std::min)(next, (std::max)(left, 0.0) + 1.0);
    }
    if (m_l.hasBarRow && !m_c.paused && m_position) {
        const double pos = m_position();
        if (m_s.showTimes) next = (std::min)(next, (1.0 - (pos - std::floor(pos))) * 1000.0 + 15.0);
        if (m_c.length > 0.0 && m_l.barW > 0.f)
            next = (std::min)(next, (std::max)(m_c.length * 1000.0 / (static_cast<double>(m_l.barW) * 2.0), 33.0));
    }
    if (m_s.fadeOnHover) next = (std::min)(next, kHoverPollMs);
    setTimer(static_cast<UINT>((std::max)(next, static_cast<double>(USER_TIMER_MINIMUM))));
}

// ---- Preparation: colours, layout, fonts, buffers ------------------------------------------

void OsdWindow::prepare() {
    resolveColors();
    computeLayout();
    initFaces();
    ensureBuffers();
}

void OsdWindow::resolveColors() {
    Colors& c = m_col;
    // The raw cover colour tints the card; the accent (bar, border) is made legible on that card.
    const bool fromCover = m_s.accentFromCover && m_c.art && m_c.art->hasAccent;
    c.accent = fromCover ? m_c.art->accent : m_s.accent;
    switch (m_s.bgMode) {
    case BgLight:
        c.bg = mixRgb(0xF4F4F7, c.accent, 0.06f);
        break;
    case BgCoverTint:
        c.bg = mixRgb(0x15151A, c.accent, 0.30f);
        break;
    case BgCoverColour:
        c.bg = mixRgb(c.accent, 0x000000, 0.18f);
        break;
    case BgCustom:
        c.bg = m_s.bgColor;
        break;
    default:
        c.bg = mixRgb(0x15151A, c.accent, 0.08f);
        break;
    }
    c.lightBg = luminance(c.bg) > 0.58f;
    if (fromCover) c.accent = accentForCard(c.accent, c.lightBg);
    c.text = m_s.textMode == TextCustom ? m_s.textColor : (c.lightBg ? 0x141418u : 0xFFFFFFu);
    c.fg = m_s.bgMode == BgCoverColour ? c.text : c.accent;
    if (c.lightBg && m_s.bgMode != BgCoverColour && luminance(c.fg) > 0.65f) c.fg = mixRgb(c.fg, 0x000000, 0.4f);
}

void OsdWindow::computeLayout() {
    Layout& l = m_l;
    const float s = l.scale;

    struct P {
        float cardW, pad, art, gap, r1, r2, r3, bar, f1, f2, f3;
        bool artTop, barFull, line3;
    } p{};
    switch (m_s.layout) {
    case LayoutCompact:
        p = {m_s.showArt ? 350.f : 300.f, 10, 56, 12, 20, 17, 0, 14, 14, 12, 11, false, false, false};
        break;
    case LayoutBanner:
        p = {m_s.showArt ? 600.f : 540.f, 12, 68, 14, 24, 20, 18, 16, 17, 14, 12, false, true, true};
        break;
    case LayoutPoster:
        p = {264.f, 14, 236, 12, 22, 19, 17, 16, 17, 13, 12, true, false, true};
        break;
    default:
        p = {m_s.showArt ? 440.f : 380.f, 14, 84, 14, 22, 19, 17, 16, 16, 13, 12, false, false, true};
        break;
    }

    // Text sizes. A picked size is exact: points to pixels at 96 dpi, times s (which already holds
    // the monitor's DPI and the Size setting). Unpicked, the layout's own size is used. Row heights
    // follow the text: each is the layout's row height scaled by how far its text moved from the
    // layout's own size.
    constexpr float kPxPerPt = 96.f / 72.f;
    const float f1 = m_s.titlePt ? static_cast<float>(m_s.titlePt) / 10.f * kPxPerPt * s : p.f1 * s;
    const float f2 = m_s.detailPt ? static_cast<float>(m_s.detailPt) / 10.f * kPxPerPt * s : p.f2 * s;
    const float f3 = m_s.detailPt ? f2 : p.f3 * s;
    const float ft = m_s.detailPt ? f2 * 0.85f : 11.f * s;
    const float k1 = f1 / (p.f1 * s), k2 = f2 / (p.f2 * s), k3 = f3 / (p.f3 * s), kt = ft / (11.f * s);

    const float R1 = p.r1 * s * k1, R2 = p.r2 * s * k2, R3 = p.r3 * s * k3, BR = p.bar * s * kt;
    l.hasRow1 = !m_c.line1.empty();
    l.hasRow2 = !m_c.line2.empty();
    l.hasRow3 = p.line3 && !m_c.line3.empty();
    l.hasBarRow = m_s.showProgress;
    l.fTitle = f1;
    l.fLine2 = f2;
    l.fLine3 = f3;
    l.fTime = ft;
    l.rowH1 = R1;
    l.rowH2 = R2;
    l.rowH3 = R3;
    l.barRowH = BR;

    const float rowGap = (m_s.layout == LayoutCompact ? 5.f : 8.f) * s;
    const bool anyLine = l.hasRow1 || l.hasRow2 || l.hasRow3;
    const bool barInText = l.hasBarRow && !p.barFull;
    const float lines = (l.hasRow1 ? R1 : 0.f) + (l.hasRow2 ? R2 : 0.f) + (l.hasRow3 ? R3 : 0.f);
    const float textH = lines + (barInText ? (anyLine ? rowGap : 0.f) + BR : 0.f);

    const float pad = p.pad * s, gap = p.gap * s;
    const bool art = m_s.showArt;
    l.shadow = m_s.shadow ? static_cast<int>(std::ceil(22.f * s)) : static_cast<int>(std::ceil(3.f * s));
    l.cardW = roundi(p.cardW * s);
    const float innerW = static_cast<float>(l.cardW) - 2.f * pad;
    const float artS = art ? static_cast<float>(roundi(p.artTop ? innerW : p.art * s)) : 0.f;

    float cardH, relArtX = pad, relArtY = pad, relTextX, textW, textTop, relBarY, regionX, regionW;
    if (!p.artTop) {
        const float contentH = maxf(artS, textH);
        const float extra = (l.hasBarRow && p.barFull) ? rowGap + BR : 0.f;
        cardH = 2.f * pad + contentH + extra;
        relArtY = pad + (contentH - artS) / 2.f;
        relTextX = art ? pad + artS + gap : pad + 6.f * s;
        textW = static_cast<float>(l.cardW) - pad - relTextX;
        textTop = pad + (contentH - textH) / 2.f;
        if (barInText) {
            relBarY = textTop + lines + (anyLine ? rowGap : 0.f);
            regionX = relTextX;
            regionW = textW;
        } else {
            relBarY = pad + contentH + rowGap;
            regionX = pad;
            regionW = innerW;
        }
    } else {
        relTextX = pad;
        textW = innerW;
        textTop = pad + (art ? artS + gap : 0.f);
        cardH = textTop + textH + pad;
        relBarY = textTop + lines + (anyLine ? rowGap : 0.f);
        regionX = pad;
        regionW = innerW;
    }

    l.cardH = roundi(cardH);
    l.winW = l.cardW + 2 * l.shadow;
    l.winH = l.cardH + 2 * l.shadow;
    l.cardX = l.cardY = static_cast<float>(l.shadow);
    l.radius = minf(static_cast<float>(m_s.cornerRadius) * s, static_cast<float>(l.cardH) / 2.f);

    const float ox = l.cardX, oy = l.cardY;
    l.artSize = artS;
    l.artX = static_cast<float>(roundi(ox + relArtX));
    l.artY = static_cast<float>(roundi(oy + relArtY));
    if (m_s.artShape == ArtCircle)
        l.artRadius = artS / 2.f;
    else if (m_s.artShape == ArtSquare)
        l.artRadius = 0.f;
    else
        l.artRadius = minf(static_cast<float>(m_s.cornerRadius) * 0.6f * s, artS / 2.f);

    l.textX = ox + relTextX;
    l.textW = textW;
    l.row1Y = oy + textTop;
    l.row2Y = l.row1Y + (l.hasRow1 ? R1 : 0.f);
    l.row3Y = l.row2Y + (l.hasRow2 ? R2 : 0.f);

    l.glyphSize = 12.f * s * k1;
    l.glyphX = l.textX + l.textW - l.glyphSize;
    l.glyphY = l.hasRow1 ? (l.row1Y + (R1 - l.glyphSize) / 2.f) : (l.row1Y + 2.f * s);

    l.barRowY = oy + relBarY;
    l.regionX = ox + regionX;
    l.regionW = regionW;
    l.timeW = 46.f * s * kt;
    if (m_s.showTimes) {
        l.barX = l.regionX + l.timeW + 4.f * s;
        l.barW = l.regionW - 2.f * l.timeW - 8.f * s;
    } else {
        l.barX = l.regionX;
        l.barW = l.regionW;
    }
    if (l.barW < 10.f) l.barW = 10.f;
    l.barH = (m_s.barStyle == BarThin ? 2.f : (m_s.barStyle == BarThick ? 7.f : 4.f)) * s;
}

void OsdWindow::initFaces() {
    const std::vector<std::string> fb{m_s.fallback1, m_s.fallback2, m_s.fallback3};
    const bool titleBold = m_s.titleWeight == 0 ? true : m_s.titleWeight >= 600;
    const bool detailBold = m_s.detailWeight >= 600;
    m_faces[0].init(m_s.titleFont, fb, m_l.fTitle, titleBold, m_s.titleItalic);
    m_faces[1].init(m_s.detailFont, fb, m_l.fLine2, detailBold, m_s.detailItalic);
    m_faces[2].init(m_s.detailFont, fb, m_l.fLine3, detailBold, m_s.detailItalic);
    m_faces[3].init(m_s.detailFont, fb, m_l.fTime, detailBold, m_s.detailItalic);
}

void OsdWindow::ensureBuffers() {
    const int w = m_l.winW, h = m_l.winH;
    if (w == m_bufW && h == m_bufH && m_dib) return;

    m_staticG.reset();
    m_frameG.reset();
    m_frameBmp.reset();
    m_staticBmp.reset();
    if (m_dib) {
        SelectObject(m_memDc, m_oldBitmap);
        DeleteObject(m_dib);
        m_dib = nullptr;
        m_bits = nullptr;
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    m_dib = CreateDIBSection(m_memDc, &bi, DIB_RGB_COLORS, &m_bits, nullptr, 0);
    if (!m_dib) return;
    m_oldBitmap = SelectObject(m_memDc, m_dib);

    const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    m_static.assign(count, 0u);
    m_shadowPx.assign(count, 0u);
    for (int& k : m_shadowKey) k = -1;
    m_frameBmp = std::make_unique<G::Bitmap>(w, h, w * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(m_bits));
    m_staticBmp = std::make_unique<G::Bitmap>(w, h, w * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(m_static.data()));
    m_frameG = std::make_unique<G::Graphics>(m_frameBmp.get());
    m_staticG = std::make_unique<G::Graphics>(m_staticBmp.get());
    setQuality(*m_frameG);
    setQuality(*m_staticG);
    m_bufW = w;
    m_bufH = h;
}

// The soft shadow depends only on the card's size and radius, so it is drawn once per size and
// copied under every repaint instead of being re-stroked.
void OsdWindow::buildShadow() {
    const Layout& l = m_l;
    const int key[6] = {l.winW, l.winH, l.cardW, l.cardH, roundi(l.radius * 4.f), m_s.shadow ? l.shadow : -2};
    bool same = true;
    for (int i = 0; i < 6; ++i) same = same && key[i] == m_shadowKey[i];
    if (same) return;
    for (int i = 0; i < 6; ++i) m_shadowKey[i] = key[i];

    std::fill(m_shadowPx.begin(), m_shadowPx.end(), 0u);
    if (!m_s.shadow) return;

    G::Bitmap bmp(l.winW, l.winH, l.winW * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(m_shadowPx.data()));
    G::Graphics g(&bmp);
    g.SetSmoothingMode(G::SmoothingModeAntiAlias);
    const G::RectF card(l.cardX, l.cardY, static_cast<float>(l.cardW), static_cast<float>(l.cardH));
    G::GraphicsPath cardPath;
    roundedPath(cardPath, card, l.radius);
    // A filled, slightly lowered silhouette of the card, blurred with three box passes (close to
    // a Gaussian), so the falloff is smooth right up to the card edge with no visible steps or
    // gaps, even on light backgrounds. The card area itself is cleared afterwards so a translucent
    // card does not show a dark patch through it.
    const float dy = 3.f * l.scale;
    G::GraphicsPath silhouette;
    roundedPath(silhouette, G::RectF(card.X, card.Y + dy, card.Width, card.Height), l.radius);
    G::SolidBrush black(G::Color(255, 0, 0, 0));
    g.FillPath(&black, &silhouette);
    g.Flush(G::FlushIntentionSync);

    const int w = l.winW, h = l.winH;
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::vector<float> a(n), t(n);
    for (size_t i = 0; i < n; ++i) a[i] = static_cast<float>(m_shadowPx[i] >> 24);
    const int r = (std::max)(1, static_cast<int>(std::floor((static_cast<float>(l.shadow) - dy) / 3.f)));
    const float inv = 1.f / static_cast<float>(2 * r + 1);
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) { // horizontal
            const float* src = &a[static_cast<size_t>(y) * w];
            float* dst = &t[static_cast<size_t>(y) * w];
            float sum = 0.f;
            for (int x = -r; x <= r; ++x) sum += (x >= 0 && x < w) ? src[x] : 0.f;
            for (int x = 0; x < w; ++x) {
                dst[x] = sum * inv;
                const int add = x + r + 1, sub = x - r;
                sum += (add < w ? src[add] : 0.f) - (sub >= 0 ? src[sub] : 0.f);
            }
        }
        for (int x = 0; x < w; ++x) { // vertical
            float sum = 0.f;
            for (int y = -r; y <= r; ++y) sum += (y >= 0 && y < h) ? t[static_cast<size_t>(y) * w + x] : 0.f;
            for (int y = 0; y < h; ++y) {
                a[static_cast<size_t>(y) * w + x] = sum * inv;
                const int add = y + r + 1, sub = y - r;
                sum += (add < h ? t[static_cast<size_t>(add) * w + x] : 0.f) - (sub >= 0 ? t[static_cast<size_t>(sub) * w + x] : 0.f);
            }
        }
    }
    const float peak = 0.36f;
    for (size_t i = 0; i < n; ++i) {
        const unsigned v = static_cast<unsigned>(a[i] * peak + 0.5f);
        m_shadowPx[i] = (v > 255u ? 255u : v) << 24; // premultiplied black
    }

    g.SetClip(&cardPath);
    g.SetCompositingMode(G::CompositingModeSourceCopy);
    g.Clear(G::Color(0, 0, 0, 0));
    g.ResetClip();
    g.Flush(G::FlushIntentionSync);
}

G::Bitmap* OsdWindow::scaledArt(int side) {
    if (!m_c.art || m_c.art->w <= 0 || m_c.art->px.empty() || side <= 0) {
        m_artScaled.reset();
        m_artKeep.reset();
        return nullptr;
    }
    if (m_artScaled && m_artKeep == m_c.art && m_artSide == side) return m_artScaled.get();

    G::Bitmap src(m_c.art->w, m_c.art->h, m_c.art->w * 4, PixelFormat32bppPARGB,
                  const_cast<BYTE*>(reinterpret_cast<const BYTE*>(m_c.art->px.data())));
    auto scaled = std::make_unique<G::Bitmap>(side, side, PixelFormat32bppPARGB);
    {
        G::Graphics sg(scaled.get());
        sg.SetInterpolationMode(G::InterpolationModeHighQualityBicubic);
        sg.SetPixelOffsetMode(G::PixelOffsetModeHalf);
        G::ImageAttributes ia;
        ia.SetWrapMode(G::WrapModeTileFlipXY);
        sg.DrawImage(&src, G::Rect(0, 0, side, side), 0, 0, m_c.art->w, m_c.art->h, G::UnitPixel, &ia);
    }
    m_artScaled = std::move(scaled);
    m_artKeep = m_c.art;
    m_artSide = side;
    return m_artScaled.get();
}

// ---- Painting ---------------------------------------------------------------------------

void OsdWindow::renderStatic() {
    if (!m_staticG || m_static.empty()) return;
    const Layout& l = m_l;
    const float s = l.scale;
    G::Graphics& g = *m_staticG;

    buildShadow();
    std::memcpy(m_static.data(), m_shadowPx.data(), m_static.size() * sizeof(std::uint32_t));

    const G::RectF card(l.cardX, l.cardY, static_cast<float>(l.cardW), static_cast<float>(l.cardH));
    G::GraphicsPath cardPath;
    roundedPath(cardPath, card, l.radius);

    // Card body, sheen, border.
    {
        G::SolidBrush fill(argb(roundi(static_cast<float>(m_s.opacity) * 2.55f), m_col.bg));
        g.FillPath(&fill, &cardPath);
        if (m_s.sheen) {
            G::LinearGradientBrush sheen(card, G::Color(m_col.lightBg ? 90 : 30, 255, 255, 255), G::Color(0, 255, 255, 255),
                                         G::LinearGradientModeVertical);
            g.FillPath(&sheen, &cardPath);
        }
        if (m_s.borderMode == BorderSubtle) {
            G::Pen border(argb(m_col.lightBg ? 40 : 46, m_col.text), 1.f);
            g.DrawPath(&border, &cardPath);
        } else if (m_s.borderMode == BorderAccent) {
            G::Pen border(argb(215, m_col.accent), maxf(1.5f * s, 1.f));
            g.DrawPath(&border, &cardPath);
        }
    }

    // Artwork, or a tinted placeholder tile.
    if (m_s.showArt && l.artSize > 0.f) {
        const G::RectF artRect(l.artX, l.artY, l.artSize, l.artSize);
        G::GraphicsPath artPath;
        if (m_s.artShape == ArtCircle)
            artPath.AddEllipse(artRect);
        else
            roundedPath(artPath, artRect, l.artRadius);

        const int side = static_cast<int>(l.artSize);
        if (G::Bitmap* scaled = scaledArt(side)) {
            G::TextureBrush tb(scaled, G::WrapModeClamp);
            tb.TranslateTransform(l.artX, l.artY, G::MatrixOrderAppend);
            g.FillPath(&tb, &artPath);
        } else {
            G::LinearGradientBrush tile(artRect, argb(255, mixRgb(m_col.accent, 0xFFFFFF, 0.15f)),
                                        argb(255, mixRgb(m_col.accent, 0x000000, 0.55f)), G::LinearGradientModeForwardDiagonal);
            g.FillPath(&tile, &artPath);
            G::FontFamily symbols(L"Segoe UI Symbol");
            G::FontFamily fallback(L"Segoe UI");
            G::Font note(symbols.IsAvailable() ? &symbols : &fallback, l.artSize * 0.42f, G::FontStyleRegular, G::UnitPixel);
            G::StringFormat sf;
            sf.SetAlignment(G::StringAlignmentCenter);
            sf.SetLineAlignment(G::StringAlignmentCenter);
            G::SolidBrush white(G::Color(215, 255, 255, 255));
            g.DrawString(L"\u266A", -1, &note, artRect, &sf, &white);
        }
        G::Pen ring(argb(m_col.lightBg ? 36 : 40, m_col.text), 1.f);
        g.DrawPath(&ring, &artPath);
    }

    // Text, with the font fallback chain.
    {
        const float glyphReserve = m_s.showGlyph ? l.glyphSize + 10.f * s : 0.f;
        if (l.hasRow1) {
            G::SolidBrush b(argb(255, m_col.text));
            m_faces[0].draw(g, m_c.line1, l.textX, l.row1Y, l.textW - glyphReserve, l.rowH1, b);
        }
        if (l.hasRow2) {
            G::SolidBrush b(argb(215, m_col.text));
            m_faces[1].draw(g, m_c.line2, l.textX, l.row2Y, l.textW, l.rowH2, b);
        }
        if (l.hasRow3) {
            G::SolidBrush b(argb(150, m_col.text));
            m_faces[2].draw(g, m_c.line3, l.textX, l.row3Y, l.textW, l.rowH3, b);
        }
    }

    // Play / pause glyph.
    if (m_s.showGlyph) {
        G::SolidBrush b(argb(255, m_col.fg));
        const float gs = l.glyphSize;
        if (m_c.paused) {
            const float bw = gs * 0.32f;
            G::GraphicsPath a, c;
            roundedPath(a, G::RectF(l.glyphX + gs * 0.06f, l.glyphY, bw, gs), bw * 0.3f);
            roundedPath(c, G::RectF(l.glyphX + gs * 0.62f, l.glyphY, bw, gs), bw * 0.3f);
            g.FillPath(&b, &a);
            g.FillPath(&b, &c);
        } else {
            const G::PointF tri[3] = {G::PointF(l.glyphX + gs * 0.12f, l.glyphY), G::PointF(l.glyphX + gs, l.glyphY + gs / 2.f),
                                      G::PointF(l.glyphX + gs * 0.12f, l.glyphY + gs)};
            g.FillPolygon(&b, tri, 3);
        }
    }
    g.Flush(G::FlushIntentionSync);
}

// Paints the elapsed/total times and the bar into the frame buffer, which must already hold a
// copy of the static layer in these rows.
void OsdWindow::drawProgress(double pos) {
    const Layout& l = m_l;
    if (!l.hasBarRow || !m_frameG) return;
    G::Graphics& g = *m_frameG;
    const float s = l.scale;

    G::SolidBrush timeBrush(argb(170, m_col.text));
    if (m_s.showTimes) m_faces[3].draw(g, formatTime(pos), l.regionX, l.barRowY, l.timeW, l.barRowH, timeBrush, false);
    if (m_c.length <= 0.0) return; // a stream: elapsed time only

    const float frac = clampf(static_cast<float>(pos / m_c.length), 0.f, 1.f);
    if (m_s.showTimes) {
        // Whole seconds on both sides, so the two labels tick over together.
        const std::wstring right = m_s.showRemaining
                                       ? L"-" + formatTime(std::floor(m_c.length) - std::floor(pos < 0.0 ? 0.0 : pos))
                                       : formatTime(m_c.length);
        m_faces[3].draw(g, right, l.regionX + l.regionW - l.timeW, l.barRowY, l.timeW, l.barRowH, timeBrush, true);
    }

    const float by = l.barRowY + (l.barRowH - l.barH) / 2.f;
    G::GraphicsPath track;
    roundedPath(track, G::RectF(l.barX, by, l.barW, l.barH), l.barH / 2.f);
    G::SolidBrush trackBrush(argb(m_col.lightBg ? 46 : 56, m_col.text));
    g.FillPath(&trackBrush, &track);

    const float fillW = l.barW * frac;
    if (fillW > 0.5f) {
        G::GraphicsPath fill;
        roundedPath(fill, G::RectF(l.barX, by, fillW < l.barH ? l.barH : fillW, l.barH), l.barH / 2.f);
        G::LinearGradientBrush fb(G::RectF(l.barX, by, l.barW, l.barH), argb(255, mixRgb(m_col.fg, 0xFFFFFF, m_col.lightBg ? 0.f : 0.25f)),
                                  argb(255, m_col.fg), G::LinearGradientModeHorizontal);
        g.FillPath(&fb, &fill);
        if (m_s.showKnob && m_s.barStyle != BarThin) {
            const float kr = (m_s.barStyle == BarThick ? 6.f : 4.5f) * s;
            G::SolidBrush knob(argb(255, m_col.lightBg ? m_col.fg : m_col.text));
            g.FillEllipse(&knob, l.barX + fillW - kr, by + l.barH / 2.f - kr, kr * 2.f, kr * 2.f);
        }
    }
}

void OsdWindow::computePlacement(int& x, int& y) const {
    const Layout& l = m_l;
    const float dist = m_s.animation == AnimSlide ? 14.f : (m_s.animation == AnimGlide ? 48.f : 0.f);
    const int off = roundi(m_slide * dist * l.scale);
    const int margin = roundi(static_cast<float>(m_s.margin) * l.scale);
    const int workW = m_work.right - m_work.left;
    const int workH = m_work.bottom - m_work.top;

    int col = 1, row = 0; // 0 left/top, 1 centre/middle, 2 right/bottom
    switch (m_s.position) {
    case TopLeft: col = 0; row = 0; break;
    case TopCenter: col = 1; row = 0; break;
    case TopRight: col = 2; row = 0; break;
    case MiddleLeft: col = 0; row = 1; break;
    case MiddleCenter: col = 1; row = 1; break;
    case MiddleRight: col = 2; row = 1; break;
    case BottomLeft: col = 0; row = 2; break;
    case BottomCenter: col = 1; row = 2; break;
    default: col = 2; row = 2; break;
    }

    if (col == 0)
        x = m_work.left + margin - l.shadow;
    else if (col == 2)
        x = m_work.right - margin - l.cardW - l.shadow;
    else
        x = m_work.left + (workW - l.cardW) / 2 - l.shadow;

    if (row == 0)
        y = m_work.top + margin - l.shadow;
    else if (row == 2)
        y = m_work.bottom - margin - l.cardH - l.shadow;
    else
        y = m_work.top + (workH - l.cardH) / 2 - l.shadow;

    // Cards at a side slide in from that side; centred ones from the top or bottom edge they sit
    // on (the middle one rises from below).
    if (col == 0)
        x -= off;
    else if (col == 2)
        x += off;
    else
        y += row == 0 ? -off : off;
}

void OsdWindow::present(bool full) {
    if (!m_hwnd || !m_frameBmp || !m_bits) return;
    const Layout& l = m_l;

    const double pos = m_position ? m_position() : 0.0;
    const float frac = (m_c.length > 0.0) ? clampf(static_cast<float>(pos / m_c.length), 0.f, 1.f) : 0.f;
    const BYTE alpha = static_cast<BYTE>(roundi(clampf(m_alpha * m_hover, 0.f, 1.f) * 255.f));
    // What the progress row shows: the whole second and the bar's half-pixel. 64-bit, as a stream
    // can run for days.
    const long long key = static_cast<long long>(pos < 0.0 ? 0.0 : pos) * 8192 +
                          (l.hasBarRow ? static_cast<long long>(frac * l.barW * 2.f) : 0);

    int x = 0, y = 0;
    computePlacement(x, y);

    const bool contentChanged = full || (l.hasBarRow && key != m_lastKey);
    const bool moved = x != m_lastPos.x || y != m_lastPos.y;
    const bool alphaChanged = alpha != m_lastAlpha;
    m_lastKey = key;
    if (!contentChanged && !moved && !alphaChanged) return;
    m_lastAlpha = alpha;
    m_lastPos = POINT{x, y};

    POINT dst{x, y};
    SIZE size{l.winW, l.winH};
    POINT src{0, 0};
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = alpha;
    bf.AlphaFormat = AC_SRC_ALPHA;

    UPDATELAYEREDWINDOWINFO info{};
    info.cbSize = sizeof(info);
    info.pptDst = &dst;
    info.pblend = &bf;
    info.dwFlags = ULW_ALPHA;

    RECT dirty{};
    if (contentChanged) {
        const size_t stride = static_cast<size_t>(l.winW) * sizeof(std::uint32_t);
        if (full) {
            std::memcpy(m_bits, m_static.data(), m_static.size() * sizeof(std::uint32_t));
        } else {
            // Only the progress row changed: restore just those rows from the static layer.
            const int y0 = static_cast<int>(std::floor(l.barRowY - 5.f * l.scale)) < 0 ? 0 : static_cast<int>(std::floor(l.barRowY - 5.f * l.scale));
            int y1 = static_cast<int>(std::ceil(l.barRowY + l.barRowH + 5.f * l.scale));
            if (y1 > l.winH) y1 = l.winH;
            dirty = RECT{0, y0, l.winW, y1};
            std::memcpy(static_cast<BYTE*>(m_bits) + static_cast<size_t>(y0) * stride,
                        reinterpret_cast<const BYTE*>(m_static.data()) + static_cast<size_t>(y0) * stride,
                        static_cast<size_t>(y1 - y0) * stride);
            info.prcDirty = &dirty;
        }
        drawProgress(pos);
        m_frameG->Flush(G::FlushIntentionSync);
        info.hdcSrc = m_memDc;
        info.pptSrc = &src;
        info.psize = &size;
    }
    // else: only alpha and/or position changed. No source surface is passed, so the window
    // manager keeps the bitmap it has and just re-blends it.
    UpdateLayeredWindowIndirect(m_hwnd, &info);
}

} // namespace osd
