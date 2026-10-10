# AGENTS.md - foo_onscreendisplay

Notes for agents working on this component. See the workspace AGENTS.md one level up for the
general rules (file tools, builds through `cmd //c`, v145 toolset, background jobs).

## Build, test, release

Then, from `foo_onscreendisplay/`:

```bat
build.bat Release x64      :: builds the SDK libraries and the component, log in build.log
build.bat Release Win32    :: the same for 32-bit
package.bat                :: builds both, dist\foo_onscreendisplay.fb2k-component (needs 7-Zip)
```

`build.bat` and `package.bat` assume Visual Studio at `C:\Program Files\Microsoft Visual Studio\18\Community`
and 7-Zip at `C:\Program Files\7-Zip`; edit the paths at the top if yours differ. If your SDK folder
has another name, change `SdkRoot` in `foo_onscreendisplay.vcxproj` and `SDK` in `build.bat`. The component links
the static C runtime, so users need no redistributable.

To try a build without packaging, copy `x64\Release\foo_onscreendisplay.dll` to
`%APPDATA%\foobar2000-v2\user-components-x64\foo_onscreendisplay\` (or `Win32\Release\foo_onscreendisplay.dll` to
`user-components\foo_onscreendisplay\` for 32-bit) and restart foobar2000.

- Release: bump the version, package, archive the PDBs as
  `../.archive/foo_onscreendisplay-<version>-symbols.zip`, commit, tag `v<version>`, push, and
  `gh release create` with the .fb2k-component attached. Only when the user says so.
- Cover colour and contrast (OKLab, APCA) live in `../fb2k-common`; change them there.
- Test in a foo_mcp instance (`../foobar2000-component-dev/references/testing-with-foo-mcp.md`):
  `python ../foo_mcp/tools/fb.py install dui64 x64/Release/<dll>`, then drive it. Never claim
  something works because it compiled; tooltips still need the user.
- Upgrading from 1.2.0 or older: the component used to be `foo_osd`; the two must not be
  installed side by side (settings carry over).

## Performance

- Loading costs one play-callback registration. GDI+, the window and the artwork subscription start
  the first time a card is shown.
- While hidden nothing runs: no timer, no polling.
- A full repaint happens only when the card is shown or its content changes. Fades only change the
  window's alpha and position. During the hold, only the progress row is repainted, and the timer
  wakes only when something can change: a second ticking over, the bar gaining a pixel, the hold
  ending (plus a 10 Hz pointer check while *Fade under the mouse pointer* is on).
- The shadow layer, scaled cover, GDI+ objects, font metrics and compiled title formatting are cached.
- Cover decoding runs on a worker thread; the UI thread never waits on it. A cover is decoded once:
  the next track of the same album reuses it. Picking its accent colour adds about 2 ms there.

## Tests

- `test\build_dialog_check.bat` creates the real preferences dialogs from `foo_onscreendisplay.rc` and reports
  truncated text, overlapping controls and controls outside the dialog. It uses the shared checker
  in `..\foobar2000-component-dev\scripts\`.
- `test\build_render_test.bat` renders every preset offline, checks the frames, times them, checks
  that every preset is recognised again, that settings and user presets survive a round trip and
  that text leaves no holes in an opaque card, prints the accent picked from synthetic covers, and
  writes contact sheets to `test\out\` (`presets.jpg` shows all of them, `accents.jpg` the covers,
  `text_zoom.jpg` the text at 3x).

## Source map

| File | Job |
| --- | --- |
| `src/component.cpp` | Identity, play callbacks, when to show, lazy start-up, menu commands |
| `src/osd_window.cpp` | The card: layouts, GDI+ drawing, animation, caching |
| `src/text_engine.cpp` | Single-line text with font fallback and ellipsis |
| `src/presets.cpp` | Built-in and user presets |
| `src/colour.h` | Forwards to `fb2k-common`: OKLab, APCA contrast, accent adjustment |
| `src/artwork.cpp` | Cover decoding and accent colour (worker-thread safe), GDI+ lifetime |
| `src/host_font.cpp` | The user's Columns UI / Default UI font |
| `src/config.cpp` | `Settings` and their persistent storage |
| `src/preferences.cpp` | The preferences page |

Settings are stored as one key=value text blob, so adding an option never breaks an existing profile.
