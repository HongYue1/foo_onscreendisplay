// foo_osd: a modern on-screen display for foobar2000 v2.
//
// Component identity, the playback controller that decides when the card shows, and the main
// menu commands. Drawing lives in osd_window.cpp, artwork decoding in artwork.cpp, text and font
// fallback in text_engine.cpp, settings in config.cpp and preferences.cpp.
//
// Performance: loading the component costs one play-callback registration. GDI+, the window,
// the artwork subscription and the artwork decoding all start the first time a card is actually
// going to be shown, and artwork is only followed while the card is enabled and shows artwork.
// A cover is decoded once per distinct image (the next track of the same album reuses it), and
// the title formatting scripts are compiled once per edit of the text lines, not once per card.

#include <helpers/foobar2000+atl.h>

#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

#include "artwork.h"
#include "config.h"
#include "controller.h"
#include "guids.h"
#include "host_font.h"
#include "osd_window.h"
#include "text_engine.h"
#include "version.h"

DECLARE_COMPONENT_VERSION(OSD_NAME, OSD_VERSION,
                          "A modern on-screen display for foobar2000 v2.\n"
                          "Album art, title, artist, album and a progress bar on a translucent card that\n"
                          "fades in when a track starts, playback is paused or you seek.\n"
                          "31 presets, four layouts, colours, fonts with a fallback chain and nine positions.\n\n"
                          "Configure it under Preferences > Tools > On-screen display.\n"
                          "Commands: View > On-screen display.");

// Stops users from renaming the DLL, which would confuse the troubleshooter.
VALIDATE_COMPONENT_FILENAME("foo_osd.dll");

namespace osd {
namespace {

class Controller;
Controller* g_controller = nullptr;

// Artwork workers still running. on_quit waits for them before GDI+ goes away.
std::atomic<int> g_workers{0};
std::atomic<bool> g_quitting{false};
std::thread g_prewarm; // one-off GDI+ font list warm-up, see lifecycle::on_init

enum class Reason { Track, StreamTitle, Pause, Seek, Manual };

// How long a new track's card may wait for its cover. The shared loader usually delivers within
// a few tens of milliseconds; a track without art simply shows after this.
constexpr double kArtWaitSeconds = 0.4;

std::wstring toWide(const char* utf8) {
    pfc::stringcvt::string_wide_from_utf8 w(utf8);
    return std::wstring(w.get_ptr());
}

// True while a full-screen game, video or presentation owns the screen.
bool fullscreenAppActive() {
    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    if (FAILED(SHQueryUserNotificationState(&state))) return false;
    return state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE;
}

bool mainWindowActive() {
    HWND fg = GetForegroundWindow();
    if (fg == nullptr || IsIconic(core_api::get_main_window())) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

bool sameImage(const album_art_data::ptr& a, const album_art_data::ptr& b) {
    if (a.is_empty() || b.is_empty()) return false;
    if (a == b) return true;
    const size_t n = a->get_size();
    return n == b->get_size() && std::memcmp(a->get_ptr(), b->get_ptr(), n) == 0;
}

// Everything here runs on the main thread: play callbacks, menu commands, the preferences page and
// the artwork hand-over all arrive there. Only decodeArtwork() runs on a worker.
class Controller : public play_callback_impl_base, public now_playing_album_art_notify {
public:
    Controller()
        : play_callback_impl_base(flag_on_playback_new_track | flag_on_playback_stop | flag_on_playback_seek |
                                  flag_on_playback_pause | flag_on_playback_dynamic_info_track) {}

    // Loaded while something is already playing (component added at runtime, or restart with
    // resume): pick up the current track so the first pause or seek has text to show.
    void init() {
        auto pc = playback_control::get();
        metadb_handle_ptr now;
        if (pc->get_now_playing(now)) {
            m_track = now;
            m_content.paused = pc->is_paused();
        }
    }

    void shutdown() {
        cancelWait();
        dropArt();
        if (m_ready) m_window.destroy();
        m_ready = false;
    }

    // ---- play_callback -------------------------------------------------------------------

    void on_playback_new_track(metadb_handle_ptr track) override {
        ++m_trackSerial;
        cancelWait();
        m_track = track;
        m_content = Content{};
        trigger(Reason::Track);
    }

    void on_playback_dynamic_info_track(const file_info&) override {
        // Internet radio: the title changed under a running stream. Keep the artwork.
        if (m_track.is_empty()) return;
        trigger(Reason::StreamTitle);
    }

    void on_playback_pause(bool paused) override {
        m_content.paused = paused;
        trigger(Reason::Pause);
    }

    void on_playback_seek(double) override { trigger(Reason::Seek); }

    void on_playback_stop(play_control::t_stop_reason reason) override {
        ++m_trackSerial;
        cancelWait();
        m_track.release();
        m_content = Content{};
        if (m_ready && reason != play_control::stop_reason_starting_another) m_window.hide();
    }

    // ---- now_playing_album_art_notify ------------------------------------------------------

    void on_album_art(album_art_data::ptr data) override {
        // The SDK does not say which thread this arrives on; make sure it is the main one.
        if (core_api::is_main_thread()) {
            onArt(std::move(data));
        } else {
            fb2k::inMainThread([data] {
                if (g_controller != nullptr) g_controller->onArt(data);
            });
        }
    }

    // ---- artwork, main thread ----------------------------------------------------------------

    void onArt(album_art_data::ptr data) {
        if (data.is_empty() || !m_following) return;
        if (sameImage(data, m_artData)) {
            // Same cover as before (next track of the same album): no decode.
            if (m_artDecodeDone) useArt(m_artDecoded);
            else m_artForTrack = m_trackSerial; // the running decode applies to this track too
            return;
        }
        m_artData = data;
        m_artDecoded.reset();
        m_artDecodeDone = false;
        m_artForTrack = m_trackSerial;
        const unsigned decode = ++m_decodeSerial;
        ++g_workers;
        fb2k::splitTask([data, decode] {
            std::shared_ptr<const Artwork> art;
            if (!g_quitting) art = decodeArtwork(data->get_ptr(), data->get_size());
            fb2k::inMainThread([art, decode] {
                if (g_controller != nullptr) g_controller->onArtDecoded(decode, art);
            });
            --g_workers;
        });
    }

    void onArtDecoded(unsigned decode, std::shared_ptr<const Artwork> art) {
        if (decode != m_decodeSerial || !m_following) return;
        m_artDecoded = art;
        m_artDecodeDone = true;
        if (m_artForTrack == m_trackSerial) useArt(art); // null: not an image, stop waiting for it
    }

    // ---- triggers ------------------------------------------------------------------------------

    void trigger(Reason reason) {
        const Settings& s = Settings::current();
        if (!s.enabled) {
            dropArt();
            return;
        }
        const bool manual = reason == Reason::Manual;
        const bool wanted = manual || (reason == Reason::Track && s.onTrack) ||
                            (reason == Reason::StreamTitle && s.onStreamTitle) ||
                            (reason == Reason::Pause && s.onPause) || (reason == Reason::Seek && s.onSeek);
        if (m_track.is_empty()) return;
        if (!wanted) {
            // Keep an already visible card truthful: new title, pause glyph.
            refreshVisible(s, reason);
            return;
        }
        if (!manual) {
            if (s.onlyWhenUnfocused && mainWindowActive()) return;
            if (s.hideInFullscreen && fullscreenAppActive()) return;
        }
        if (!ensureReady()) return;
        if (s.showArt) followArt();
        else dropArt();

        // A new track's card waits briefly for its cover, so it does not appear and then jump to a
        // new layout a moment later.
        if (reason == Reason::Track && s.showArt && s.waitForArt && m_following && !m_content.art) {
            m_waiting = true;
            if (m_waitTimer.is_empty()) {
                m_waitTimer = fb2k::registerTimer(kArtWaitSeconds, [] {
                    if (g_controller != nullptr) g_controller->flushWait();
                });
            }
            return;
        }
        cancelWait();
        display(s, false);
    }

    void preview(const Settings& s) {
        if (!ensureReady()) return;
        if (s.showArt) followArt();
        display(s, true);
    }

    void hideNow() {
        cancelWait();
        if (m_ready) m_window.hide();
    }

    // The stored settings changed (preferences applied, menu toggle).
    void settingsChanged() {
        const Settings& s = Settings::current();
        m_scripts.invalidate();
        if (!s.enabled) {
            hideNow();
            dropArt();
        } else if (!s.showArt) {
            dropArt();
        }
    }

    void flushWait() {
        if (!m_waiting) return;
        cancelWait();
        if (!m_content.art && m_following && m_artDecodeDone) {
            // No notification for this track: the loader may skip one for an unchanged image
            // (next track of the same album). Its current art tells.
            try {
                if (sameImage(now_playing_album_art_notify_manager::get()->current(), m_artData))
                    m_content.art = m_artDecoded;
            } catch (...) {
            }
        }
        const Settings& s = Settings::current();
        if (s.enabled && m_track.is_valid()) display(s, false);
    }

private:
    // Compiled title formatting for the three lines, recompiled only when a line's text changes.
    struct Scripts {
        std::string spec[3];
        titleformat_object::ptr obj[3];
        bool compiled[3] = {false, false, false};

        void invalidate() {
            for (int i = 0; i < 3; ++i) {
                compiled[i] = false;
                obj[i].release();
            }
        }
        const titleformat_object::ptr& get(int i, const std::string& text) {
            if (!compiled[i] || spec[i] != text) {
                spec[i] = text;
                obj[i].release();
                if (!text.empty()) titleformat_compiler::get()->compile_safe_ex(obj[i], text.c_str());
                compiled[i] = true;
            }
            return obj[i];
        }
    };

    bool ensureReady() {
        if (m_ready) return true;
        if (m_failed) return false;
        if (!startGdiplus()) {
            m_failed = true;
            console::error("foo_osd: GDI+ failed to start, the on-screen display is disabled");
            return false;
        }
        if (!m_window.create()) {
            m_failed = true;
            console::error("foo_osd: could not create the OSD window");
            return false;
        }
        m_window.setPositionSource([this] { return currentPosition(); });
        m_ready = true;
        return true;
    }

    // Artwork is only followed while a card that shows it can appear.
    void followArt() {
        if (m_following) return;
        try {
            auto mgr = now_playing_album_art_notify_manager::get();
            mgr->add(this);
            m_following = true;
            // Something already playing: its art is loaded, pick it up.
            album_art_data::ptr art = mgr->current();
            if (art.is_valid()) onArt(std::move(art));
        } catch (...) {
            m_following = false; // no artwork, everything else still works
        }
    }

    void dropArt() {
        if (m_following) {
            try {
                now_playing_album_art_notify_manager::get()->remove(this);
            } catch (...) {
            }
            m_following = false;
        }
        ++m_decodeSerial; // a decode still running is no longer wanted
        m_artData.release();
        m_artDecoded.reset();
        m_artDecodeDone = false;
        m_content.art.reset();
        if (m_waiting) flushWait();
    }

    void useArt(const std::shared_ptr<const Artwork>& art) {
        m_content.art = art;
        if (m_waiting) {
            flushWait();
        } else if (art && m_ready && m_window.visible()) {
            m_window.setContent(m_content);
        }
    }

    void cancelWait() {
        m_waiting = false;
        m_waitTimer.release();
    }

    void formatLines(const Settings& s) {
        auto pc = playback_control::get();
        const std::string* specs[3] = {&s.line1, &s.line2, &s.line3};
        std::wstring* lines[3] = {&m_content.line1, &m_content.line2, &m_content.line3};
        pfc::string8 out;
        for (int i = 0; i < 3; ++i) {
            const titleformat_object::ptr& script = m_scripts.get(i, *specs[i]);
            lines[i]->clear();
            if (script.is_empty()) continue;
            if (pc->playback_format_title_ex(m_track, nullptr, out, script, nullptr, playback_control::display_level_all))
                *lines[i] = toWide(out.c_str());
        }
        const double length = m_track->get_length();
        m_content.length = length > 0.0 ? length : 0.0;
    }

    void refreshVisible(const Settings& s, Reason reason) {
        if (!m_ready || !m_window.visible() || m_waiting) return;
        if (reason == Reason::Track || reason == Reason::StreamTitle) formatLines(s);
        m_window.setContent(m_content);
    }

    void display(const Settings& s, bool preview) {
        if (!ensureReady()) return;
        Content c;
        m_fakeProgress = false;
        if (m_track.is_valid()) {
            if (preview) {
                const Content saved = m_content;
                formatLines(s); // with the dialog's unsaved lines
                c = m_content;
                m_content = saved;
            } else {
                formatLines(s);
                c = m_content;
            }
        } else {
            // Nothing is playing: show sample text so the user can judge the look.
            c.line1 = L"Song title goes here";
            c.line2 = L"Artist name";
            c.line3 = L"Album name (2026)";
            c.length = 215.0;
            m_fakeProgress = true;
        }
        // Unpicked fonts follow the user's UI font (Columns UI, else Default UI).
        Settings shown = s;
        applyHostFonts(shown);
        m_window.show(shown, c, core_api::get_main_window());
    }

    double currentPosition() const {
        if (m_fakeProgress) return 78.0;
        return playback_control::get()->playback_get_position();
    }

    OsdWindow m_window;
    Content m_content; // what the card shows for the playing track
    metadb_handle_ptr m_track;
    Scripts m_scripts;
    unsigned m_trackSerial = 0;  // bumped per track and on stop
    unsigned m_decodeSerial = 0; // bumped per decode started and when art is dropped
    unsigned m_artForTrack = 0;  // the track the current art data arrived for
    album_art_data::ptr m_artData;            // last cover received
    std::shared_ptr<const Artwork> m_artDecoded; // ... and its decoded form
    bool m_artDecodeDone = false;
    bool m_following = false; // registered with the now-playing artwork manager
    bool m_waiting = false;   // a track card is waiting for its cover
    fb2k::objRef m_waitTimer;
    bool m_fakeProgress = false;
    bool m_ready = false;
    bool m_failed = false;
};

class lifecycle : public initquit {
public:
    void on_init() override {
        auto controller = std::make_unique<Controller>();
        controller->init();
        g_controller = controller.release();

        // GDI+ needs about 300 ms the first time it builds its font list. When the display is on,
        // pay that on a background thread a little after start-up rather than on the first card.
        if (Settings::current().enabled) {
            g_prewarm = std::thread([] {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                for (int i = 0; i < 40 && !g_quitting; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (!g_quitting) prewarmFonts();
            });
        }
    }

    void on_quit() override {
        g_quitting = true;
        if (g_prewarm.joinable()) g_prewarm.join();
        if (g_controller != nullptr) {
            Controller* c = g_controller;
            g_controller = nullptr;
            c->shutdown();
            delete c;
        }
        // Give artwork workers a moment to leave GDI+ before it is shut down.
        for (int i = 0; i < 200 && g_workers.load() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (g_workers.load() == 0) {
            text::shutdown();
            stopGdiplus();
        }
    }
};

FB2K_SERVICE_FACTORY(lifecycle);

// ---- Main menu: View > On-screen display ---------------------------------------------------

mainmenu_group_popup_factory g_menuGroup(guids::mainmenu_group, mainmenu_groups::view, mainmenu_commands::sort_priority_dontcare,
                                         "On-screen display");

class menuCommands : public mainmenu_commands {
public:
    enum { cmdShow = 0, cmdHide, cmdToggle, cmdCount };

    t_uint32 get_command_count() override { return cmdCount; }

    GUID get_command(t_uint32 index) override {
        switch (index) {
        case cmdShow: return guids::cmd_show;
        case cmdHide: return guids::cmd_hide;
        case cmdToggle: return guids::cmd_toggle;
        default: uBugCheck();
        }
    }

    void get_name(t_uint32 index, pfc::string_base& out) override {
        switch (index) {
        case cmdShow: out = "Show now"; break;
        case cmdHide: out = "Hide"; break;
        case cmdToggle: out = "Enabled"; break;
        default: uBugCheck();
        }
    }

    bool get_description(t_uint32 index, pfc::string_base& out) override {
        switch (index) {
        case cmdShow: out = "Shows the on-screen display for the playing track."; return true;
        case cmdHide: out = "Hides the on-screen display if it is showing."; return true;
        case cmdToggle: out = "Turns the on-screen display on or off."; return true;
        default: return false;
        }
    }

    GUID get_parent() override { return guids::mainmenu_group; }

    void execute(t_uint32 index, service_ptr_t<service_base>) override {
        switch (index) {
        case cmdShow: showNow(); break;
        case cmdHide: hideNow(); break;
        case cmdToggle: toggleEnabled(); break;
        default: uBugCheck();
        }
    }

    bool get_display(t_uint32 index, pfc::string_base& text, t_uint32& flags) override {
        const bool ok = mainmenu_commands::get_display(index, text, flags);
        if (!ok) return false;
        const Settings& s = Settings::current();
        if (index == cmdToggle && s.enabled) flags |= flag_checked;
        if (index == cmdShow && !s.enabled) flags |= flag_disabled;
        return true;
    }
};

mainmenu_commands_factory_t<menuCommands> g_menuCommands;

} // namespace

void showPreview(const Settings& settings) {
    if (g_controller != nullptr) g_controller->preview(settings);
}

void showNow() {
    if (g_controller != nullptr) g_controller->trigger(Reason::Manual);
}

void hideNow() {
    if (g_controller != nullptr) g_controller->hideNow();
}

void settingsChanged() {
    if (g_controller != nullptr) g_controller->settingsChanged();
}

void toggleEnabled() {
    Settings s = Settings::current();
    s.enabled = !s.enabled;
    s.save();
    settingsChanged();
}

} // namespace osd
