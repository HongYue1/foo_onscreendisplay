#pragma once

#include "config.h"

namespace osd {

// Entry points for the UI (preferences page, menu commands). All main thread.

//! Show the card now using these settings, even if the OSD is disabled. Uses the track that is
//! playing, or sample text when nothing is.
void showPreview(const Settings& settings);

//! Show the card now with the saved settings (menu command). Does nothing while stopped.
void showNow();

//! Fade the card out if it is showing (menu command).
void hideNow();

//! Call after Settings::save(): hides the card when the OSD was turned off, stops following
//! artwork when it is no longer shown, and drops cached title formatting.
void settingsChanged();

//! Flip Settings::enabled, save it and apply it.
void toggleEnabled();

} // namespace osd
