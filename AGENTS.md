# AGENTS.md - foo_onscreendisplay

## Build and test

```bat
build.bat Release x64      :: SDK libraries + component, log in build.log
build.bat Release Win32
package.bat                :: both, dist\foo_onscreendisplay.fb2k-component (needs 7-Zip)
test\build_dialog_check.bat :: preferences dialogs: truncated text, overlaps, out of bounds
test\build_render_test.bat  :: renders every preset offline, round trips, timings, contact sheets in test\out\
```

`build.bat` / `package.bat` hold the Visual Studio and 7-Zip paths at the top; `SdkRoot` in the
`.vcxproj` and `SDK` in `build.bat` name the SDK folder. Static CRT.

## Reminders

- Cover colour and contrast (OKLab, APCA) come from `../fb2k-common`; change them there.
- Up to 1.2.0 the component was `foo_osd`; the two must not be installed side by side (settings
  carry over).
- Settings are one key=value text blob: adding an option never breaks an existing profile.

## Performance

- Loading costs one play-callback registration. GDI+, the window and the artwork subscription start
  the first time a card is shown. While hidden nothing runs: no timer, no polling.
- A full repaint happens only when the card is shown or its content changes. Fades only change the
  window's alpha and position. During the hold only the progress row is repainted, and the timer
  wakes only when something can change: a second ticking over, the bar gaining a pixel, the hold
  ending (plus a 10 Hz pointer check while *Fade under the mouse pointer* is on).
- The shadow layer, scaled cover, GDI+ objects, font metrics and compiled title formatting are cached.
- Cover decoding runs on a worker thread; the UI thread never waits on it. A cover is decoded once:
  the next track of the same album reuses it. Picking its accent colour adds about 2 ms there.

## Source map

| File | Job |
| --- | --- |
| `src/component.cpp` | Identity, play callbacks, when to show, lazy start-up, menu commands |
| `src/osd_window.cpp` | The card: layouts, GDI+ drawing, animation, caching |
| `src/text_engine.cpp` | Single-line text with font fallback and ellipsis |
| `src/presets.cpp` | Built-in and user presets |
| `src/colour.h` | Forwards to `fb2k-common`: OKLab, APCA contrast, accent adjustment |
| `src/artwork.cpp` | Cover decoding and accent colour (worker-thread safe), GDI+ lifetime |
| `src/host_font.cpp` | The Columns UI / Default UI font |
| `src/config.cpp` | `Settings` and their persistent storage |
| `src/preferences.cpp` | The preferences page |
