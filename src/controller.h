#pragma once

#include "config.h"

namespace osd {

// Entry points for the UI (preferences page, menu commands). All main thread.

//! Show the card now using these settings, even if the OSD is disabled. Uses the track that is
//! playing, or sample text when nothing is.
void showPreview(const Settings& settings);

//! Show the card now with the saved settings (menu command).
void showNow();

//! Flip Settings::enabled and save it.
void toggleEnabled();

} // namespace osd
