<h1 align="center">foo_osd</h1>

<p align="center">
  A modern on-screen display for <a href="https://www.foobar2000.org/">foobar2000</a> v2.<br />
</p>

<p align="center">
  <img src="docs/images/accent-from-cover.webp" alt="foo_osd card with a violet accent taken from the cover art" width="640" />
</p>

## Features

- **21 presets** and four layouts (Classic, Compact, Banner, Poster), or build your own look.
- **Accent from the cover.** The dominant colour of the artwork drives the progress bar and outline,
  and is adjusted to stay readable. Greyscale covers get an off-white (dark card) or charcoal
  (light card) accent instead of an arbitrary colour.
- **Your fonts.** Pick line 1 and lines 2-3 with the standard Windows font dialog (family, style, exact
  size). Unpicked fonts follow your Columns UI or Default UI font. Up to three **fallback fonts** cover
  characters the main font lacks (Cyrillic, CJK, symbols, emoji), on all lines.
- **Your text.** Three lines written in foobar2000 title formatting.
- **Well behaved.** Click-through, always on top, never takes focus. Follows the monitor foobar2000 is
  on and per-monitor DPI. Optionally shows only while foobar2000 is in the background, and stays away
  from full-screen apps.
- **Light on resources.** Nothing runs while the card is hidden, and only the progress row is
  repainted while it is shown. See [Performance](#performance).

<table>
  <tr>
    <td width="50%"><img src="docs/images/glass.webp" alt="Translucent card" width="100%" /><br /><sub>Translucent card, paused</sub></td>
    <td width="50%"><img src="docs/images/snow.webp" alt="Light card" width="100%" /><br /><sub>Light card</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/amoled.webp" alt="Compact black card" width="100%" /><br /><sub>Compact black card</sub></td>
    <td width="50%"><img src="docs/images/accent-from-cover.webp" alt="Dark card with a violet outline" width="100%" /><br /><sub>Accent colour from the cover</sub></td>
  </tr>
</table>

## Install

Requires foobar2000 v2 (64-bit) on Windows.

1. Download `foo_osd.fb2k-component` from the [latest release](https://github.com/HongYue1/foo_osd/releases/latest).
2. Double-click it, or in foobar2000 open **Preferences > Components > Install...**, and restart.

To remove it, use **Preferences > Components**.

## Use

The card appears when a track starts, when playback is paused or resumed, and (if you turn it on)
when you seek.

- **Settings:** Preferences > Tools > **On-screen display**, on four tabs: *General*, *Appearance*,
  *Text* and *Fonts*.
- **Menu:** View > On-screen display > *Show now* / *Enabled*.
- **Preview:** shows the card with the values on the page, before you press Apply. Picking a preset
  applies its look to the page and previews it.

### Tabs

| Tab | What is in it |
| --- | --- |
| General | What triggers the card, position, margin, size (50-300%), opacity, display time; follow foobar2000's monitor, hide over full-screen apps, only when in the background |
| Appearance | Layout, background (dark, light, cover tint, cover colour, custom), border, corner radius, art shape, text colour, accent, animation and speed, bar style, and which elements show (art, progress bar, times, play/pause icon, knob, shadow, glossy edge, accent from cover) |
| Text | The three track lines. **Help** opens foobar2000's title formatting reference; **Default** restores the three lines only |
| Fonts | Line 1 font, lines 2-3 font, and three fallback fonts |

### Track text

The defaults are the title, the artist, and the album with its year:

```
$if2(%title%,%filename%)
%artist%
%album%[ '('%date%')']
```

An empty line is left out. In title formatting, literal brackets need quotes: `'('%date%')'`.

### Presets

A preset only sets the look. It never changes your fonts, size, margin, position, behaviour or
track text.

| Preset | Look |
| --- | --- |
| **Midnight** (default) | Compact black card, square cover, thin bar, soft shadow |
| Glass | Frosted, see-through dark pane |
| Snow | Light card |
| Cover colour | The card takes the cover's colour |
| Cover tint | Dark card washed with the cover's colour |
| AMOLED | Pure black, no shadow |
| Neon | Dark card, accent outline, thick bar |
| Compact | Small one-row card |
| Banner | Wide and low, one row |
| Poster | Large cover on top |
| Circle | Round cover, rounded card |
| Pill | Capsule with a round cover, no progress bar |
| Flat | Square corners, no shadow or gloss |
| Retro terminal | Green on near-black, no cover |
| Sunset, Ocean, Forest, Rose | Coloured themes with their own text tint |
| Paper | Light and plain |
| Bold | Near-black, accent outline, chunky bar |
| Quiet | Small, translucent, no shadow, gentle fade |

### Fonts

Line 1 defaults to **11 pt bold** and lines 2-3 to **9 pt**, in your UI font. The size you pick is
exact (at 100% Size). Fallback fonts are tried in order for characters the main font cannot draw,
then built-in system fonts for CJK, symbols and emoji. A fallback only lends its family: size,
weight and italic come from the line it is drawing.

## Performance

- Loading costs one play-callback registration. GDI+, the window and the artwork subscription start
  the first time a card is shown.
- While hidden nothing runs: no timer, no polling.
- A full repaint happens only when the card is shown or its content changes. Fades only change the
  window's alpha and position. During the hold, only the progress row is repainted (4 Hz, and only
  when a pixel or a second changed).
- The shadow layer, scaled cover, GDI+ objects and font metrics are cached.
- Cover decoding runs on a worker thread; the UI thread never waits on it.

## Building

Windows, Visual Studio 2022 or later with the *Desktop development with C++* workload.

The project builds against sibling folders rather than vendored copies:

```
some-folder/
  foo_osd/             this repository
  SDK-2026-09-17/      foobar2000 SDK, with the Columns UI SDK cloned inside as columns_ui-sdk/
  wtl/                 WTL (the folder that contains Include/)
```

- foobar2000 SDK: <https://www.foobar2000.org/SDK>
- Columns UI SDK: <https://github.com/reupen/columns_ui-sdk>
- WTL: <https://sourceforge.net/projects/wtl/>

Then, from `foo_osd/`:

```bat
build.bat Release x64      :: builds the SDK libraries and the component, log in build.log
package.bat                :: dist\foo_osd.fb2k-component (needs 7-Zip)
```

`build.bat` and `package.bat` assume Visual Studio at `C:\Program Files\Microsoft Visual Studio\18\Community`
and 7-Zip at `C:\Program Files\7-Zip`; edit the paths at the top if yours differ. If your SDK folder
has another name, change `SdkRoot` in `foo_osd.vcxproj` and `SDK` in `build.bat`. The component links
the static C runtime, so users need no redistributable.

To try a build without packaging, copy `x64\Release\foo_osd.dll` to
`%APPDATA%\foobar2000-v2\user-components-x64\foo_osd\` and restart foobar2000.

### Tests (no foobar2000 needed)

- `test\build_dialog_check.bat` creates the real preferences dialog from `foo_osd.rc` and reports
  truncated text, overlapping controls and controls outside the dialog.
- `test\build_render_test.bat` renders every preset offline, checks the frames, times them and prints
  an ASCII view of a few layouts.

### Source map

| File | Job |
| --- | --- |
| `src/component.cpp` | Identity, play callbacks, when to show, lazy start-up, menu commands |
| `src/osd_window.cpp` | The card: layouts, GDI+ drawing, animation, caching |
| `src/text_engine.cpp` | Single-line text with font fallback and ellipsis |
| `src/presets.cpp` | The presets |
| `src/artwork.cpp` | Cover decoding and accent colour (worker-thread safe), GDI+ lifetime |
| `src/host_font.cpp` | The user's Columns UI / Default UI font |
| `src/config.cpp` | `Settings` and their persistent storage |
| `src/preferences.cpp` | The preferences page |

Settings are stored as one key=value text blob, so adding an option never breaks an existing profile.

## License

[MIT](LICENSE)
