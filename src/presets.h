#pragma once

#include <string>

#include "config.h"

// Built-in presets come first (indices 0 .. builtinCount() - 1), then the user's own, in the order
// they were saved. Main thread only: user presets are loaded from the profile on first use.
namespace osd::presets {

int builtinCount();
int count();
const char* name(int index);
bool isUser(int index);
//! Built-in: resets the look to the default, then applies the preset on top. User preset: copies
//! its stored look. Either way behaviour, placement, the text lines and the fonts stay.
void apply(int index, Settings& settings);
//! The preset whose look these settings have, or -1 when the look has been changed by hand.
int match(const Settings& settings);
//! True when the preset at index would leave these settings as they are.
bool matches(int index, const Settings& settings);
//! Index of the preset with this name (ASCII case-insensitive), or -1.
int find(const std::string& name);

//! False for an empty name or one a built-in preset uses.
bool validUserName(const std::string& name);
//! Stores the look of settings as a user preset, replacing a user preset of the same name.
//! Returns its index, or -1 when the name is not valid.
int saveUser(const std::string& name, const Settings& settings);
bool removeUser(int index);

} // namespace osd::presets
