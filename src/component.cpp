// foo_osd: a modern on-screen display for foobar2000 v2.
//
// Component identity, the playback controller that decides when the card shows, and the main
// menu commands. Drawing lives in osd_window.cpp, artwork decoding in artwork.cpp, text and font
// fallback in text_engine.cpp, settings in config.cpp and preferences.cpp.
//
// Performance: loading the component costs one play-callback registration. GDI+, the window,
// the artwork subscription and the artwork decoding all start the first time a card is actually
// going to be shown, and artwork is only followed while the card is enabled and shows artwork.

#include <helpers/foobar2000+atl.h>

#include <shellapi.h>

#include <atomic>
#include <chrono>
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
                          "Presets, layouts, colours and fonts with a fallback chain.\n\n"
                          "Configure it under Preferences > Tools > On-screen display.");

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

enum class Reason { Track, Pause, Seek, Manual };

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

class Controller : public play_callback_impl_base {
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
        dropArt();
        if (m_ready) m_window.destroy();
        m_ready = false;
    }

    // ---- play_callback -------------------------------------------------------------------

    void on_playback_new_track(metadb_handle_ptr track) override {
        ++m_serial;
        m_track = track;
        m_content = Content{};
        trigger(Reason::Track);
    }

    void on_playback_dynamic_info_track(const file_info&) override {
        // Internet radio: the title changed under a running stream. Keep the artwork.
        if (m_track.is_empty()) return;
        trigger(Reason::Track);
    }

    void on_playback_pause(bool paused) override {
        m_content.paused = paused;
        trigger(Reason::Pause);
    }

    void on_playback_seek(double) override { trigger(Reason::Seek); }

    void on_playback_stop(play_control::t_stop_reason reason) override {
        ++m_serial;
        m_track.release();
        m_content = Content{};
        if (m_ready && reason != play_control::stop_reason_starting_another) m_window.hide();
    }

    // ---- called from the artwork worker and the UI ----------------------------------------

    void onArtDecoded(unsigned serial, std::shared_ptr<const Artwork> art) {
        if (serial != m_serial || !art) return;
        m_content.art = std::move(art);
        if (m_ready && m_window.visible()) m_window.setContent(m_content);
    }

    unsigned serial() const { return m_serial.load(); }

    void trigger(Reason reason) {
        const Settings s = Settings::load();
        if (!s.enabled) {
            dropArt();
            return;
        }
        const bool manual = reason == Reason::Manual;
        const bool wanted = (reason == Reason::Track && s.onTrack) || (reason == Reason::Pause && s.onPause) ||
                            (reason == Reason::Seek && s.onSeek) || manual;
        if (!wanted) {
            // Still keep an already visible card truthful (pause glyph).
            if (m_ready && m_window.visible()) m_window.setContent(m_content);
            return;
        }
        if (!manual) {
            if (m_track.is_empty()) return;
            if (s.onlyWhenUnfocused && mainWindowActive()) return;
            if (s.hideInFullscreen && fullscreenAppActive()) return;
        }
        display(s, false);
    }

    void preview(const Settings& s) { display(s, true); }

private:
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
        if (m_artNotify != nullptr) return;
        try {
            auto mgr = now_playing_album_art_notify_manager::get();
            m_artNotify = mgr->add([](album_art_data::ptr data) { onArtData(std::move(data)); });
            // Something already playing: the art is loaded, pick it up.
            album_art_data::ptr art = mgr->current();
            if (art.is_valid()) onArtData(std::move(art));
        } catch (...) {
            m_artNotify = nullptr; // no artwork, everything else still works
        }
    }

    void dropArt() {
        if (m_artNotify == nullptr) return;
        try {
            now_playing_album_art_notify_manager::get()->remove(m_artNotify);
        } catch (...) {
        }
        m_artNotify = nullptr;
        m_content.art.reset();
    }

    void display(const Settings& s, bool preview) {
        if (!ensureReady()) return;
        if (s.showArt) followArt();
        else if (!preview) dropArt();

        Content c = m_content;
        m_fakeProgress = false;
        if (m_track.is_valid()) {
            const Content formatted = buildContent(m_track, s);
            c.line1 = formatted.line1;
            c.line2 = formatted.line2;
            c.line3 = formatted.line3;
            c.length = formatted.length;
            if (!preview) {
                m_content.line1 = c.line1;
                m_content.line2 = c.line2;
                m_content.line3 = c.line3;
                m_content.length = c.length;
            }
        } else {
            // Nothing is playing: show sample text so the user can judge the look.
            c.line1 = L"Song title goes here";
            c.line2 = L"Artist name";
            c.line3 = L"Album name (2026)";
            c.length = 215.0;
            c.paused = false;
            m_fakeProgress = true;
        }
        // Unpicked fonts follow the user's UI font (Columns UI, else Default UI).
        Settings shown = s;
        applyHostFonts(shown);
        m_window.show(shown, c, core_api::get_main_window());
    }

    // Title-formats the three lines. Compiled per card: it is cheap, and it means edits made on the
    // preferences page apply to the next card without any invalidation logic.
    static Content buildContent(const metadb_handle_ptr& track, const Settings& s) {
        Content c;
        auto pc = playback_control::get();
        auto format = [&](const std::string& spec) -> std::wstring {
            if (spec.empty()) return std::wstring();
            titleformat_object::ptr script;
            titleformat_compiler::get()->compile_safe_ex(script, spec.c_str());
            pfc::string8 out;
            if (!pc->playback_format_title_ex(track, nullptr, out, script, nullptr, playback_control::display_level_all))
                return std::wstring();
            return toWide(out.c_str());
        };
        c.line1 = format(s.line1);
        c.line2 = format(s.line2);
        c.line3 = format(s.line3);
        c.length = track.is_valid() ? track->get_length() : 0.0;
        if (c.length < 0.0) c.length = 0.0;
        return c;
    }

    double currentPosition() const {
        if (m_fakeProgress) return 78.0;
        return playback_control::get()->playback_get_position();
    }

    static void onArtData(album_art_data::ptr data) {
        // The manager may call from any thread; do the decode on a worker and the hand-over on
        // the main thread.
        if (g_controller == nullptr || data.is_empty()) return;
        const unsigned serial = g_controller->serial();
        ++g_workers;
        fb2k::splitTask([data, serial] {
            std::shared_ptr<const Artwork> art;
            if (!g_quitting) art = decodeArtwork(data->get_ptr(), data->get_size());
            --g_workers;
            if (!art) return;
            fb2k::inMainThread([art, serial] {
                if (g_controller != nullptr) g_controller->onArtDecoded(serial, art);
            });
        });
    }

    OsdWindow m_window;
    Content m_content;
    metadb_handle_ptr m_track;
    std::atomic<unsigned> m_serial{0};
    std::atomic<bool> m_fakeProgress{false};
    now_playing_album_art_notify* m_artNotify = nullptr;
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
        if (Settings::load().enabled) {
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
    enum { cmdShow = 0, cmdToggle, cmdCount };

    t_uint32 get_command_count() override { return cmdCount; }

    GUID get_command(t_uint32 index) override {
        switch (index) {
        case cmdShow: return guids::cmd_show;
        case cmdToggle: return guids::cmd_toggle;
        default: uBugCheck();
        }
    }

    void get_name(t_uint32 index, pfc::string_base& out) override {
        switch (index) {
        case cmdShow: out = "Show now"; break;
        case cmdToggle: out = "Enabled"; break;
        default: uBugCheck();
        }
    }

    bool get_description(t_uint32 index, pfc::string_base& out) override {
        switch (index) {
        case cmdShow: out = "Shows the on-screen display for the current track."; return true;
        case cmdToggle: out = "Turns the on-screen display on or off."; return true;
        default: return false;
        }
    }

    GUID get_parent() override { return guids::mainmenu_group; }

    void execute(t_uint32 index, service_ptr_t<service_base>) override {
        switch (index) {
        case cmdShow: showNow(); break;
        case cmdToggle: toggleEnabled(); break;
        default: uBugCheck();
        }
    }

    bool get_display(t_uint32 index, pfc::string_base& text, t_uint32& flags) override {
        const bool ok = mainmenu_commands::get_display(index, text, flags);
        if (ok && index == cmdToggle && Settings::load().enabled) flags |= flag_checked;
        return ok;
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

void toggleEnabled() {
    Settings s = Settings::load();
    s.enabled = !s.enabled;
    s.save();
}

} // namespace osd
