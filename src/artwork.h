#pragma once

#include <cstddef>
#include <memory>

#include "osd_window.h"

namespace osd {

// GDI+ lifetime. Call startGdiplus() once from on_init and stopGdiplus() from on_quit, after every
// OsdWindow and every decodeArtwork() call is finished.
bool startGdiplus();
void stopGdiplus();

// Starts GDI+ if needed and pays its one-off font-list cost (about 300 ms). Meant for a
// background thread that is joined before stopGdiplus().
void prewarmFonts();

// Decode an encoded image (JPEG, PNG, ...) into a centre-cropped, kSide x kSide premultiplied
// bitmap and pick an accent colour from it. Safe to call from a worker thread. Returns null when
// the data is not an image GDI+ understands.
std::shared_ptr<const Artwork> decodeArtwork(const void* data, std::size_t size);

} // namespace osd
