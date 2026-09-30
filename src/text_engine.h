#pragma once

// Single-line text with an ordered font fallback chain, on GDI+.
//
// GDI+ has no fallback order of its own: a character the font lacks is drawn with whatever
// Windows picks. This splits a string into runs by which font in *our* chain covers each
// character (title/detail font, then fallback 1-3, then a built-in list of system fonts for CJK,
// symbols and emoji), aligns the runs on one baseline, and truncates with an ellipsis.

#include <memory>
#include <string>
#include <vector>

namespace Gdiplus {
class Graphics;
class Brush;
} // namespace Gdiplus

namespace osd::text {

class Faces {
public:
    Faces();
    ~Faces();
    Faces(const Faces&) = delete;
    Faces& operator=(const Faces&) = delete;

    //! primary may be empty (Segoe UI). Fonts are created lazily, only when a run needs them.
    void init(const std::string& primaryUtf8, const std::vector<std::string>& fallbacksUtf8, float emPixels, bool bold,
              bool italic = false);

    //! ClearType needs an opaque background under the text and an opaque brush; without it the
    //! text is drawn with hinted greyscale anti-aliasing. Either way glyphs sit on whole pixels.
    void setClearType(bool on);

    //! Draws one line inside the box, vertically centred on the primary font's baseline.
    //! Truncates with an ellipsis when wider than w. Returns the width drawn.
    float draw(Gdiplus::Graphics& g, const std::wstring& text, float x, float y, float w, float h,
               const Gdiplus::Brush& brush, bool alignRight = false);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

//! Release the family cache. Call before GDI+ shuts down.
void shutdown();

} // namespace osd::text
