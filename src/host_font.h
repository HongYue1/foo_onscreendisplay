#pragma once

#include <string>

#include "config.h"

namespace osd {

//! Family name of the user's UI font: Columns UI's common labels font when Columns UI is
//! installed, otherwise Default UI's font. Empty when neither could be read. Main thread only.
std::string hostFontFamily();

//! Fills an empty title/detail font family with hostFontFamily(), so the card follows the UI
//! font unless the user picked one. Apply to the copy handed to the window, never to stored settings.
void applyHostFonts(Settings& settings);

} // namespace osd
