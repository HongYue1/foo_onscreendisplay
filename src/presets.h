#pragma once

#include "config.h"

namespace osd::presets {

int count();
const char* name(int index);
//! Resets the look to the default, then applies the preset on top. Leaves behaviour, placement,
//! the text lines and the fonts alone.
void apply(int index, Settings& settings);
//! The preset whose look these settings have, or -1 when the look has been changed by hand.
int match(const Settings& settings);

} // namespace osd::presets
