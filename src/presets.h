#pragma once

#include "config.h"

namespace osd::presets {

int count();
const char* name(int index);
//! Resets the look to the default, then applies the preset on top. Leaves behaviour, the text
//! lines and the fallback fonts alone.
void apply(int index, Settings& settings);

} // namespace osd::presets
