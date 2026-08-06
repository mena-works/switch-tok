#pragma once

#include <string>

// Preferences that survive a restart, kept on the SD card.
//
// Small and deliberately forgiving: a missing or malformed file is not an
// error, it just means defaults. Losing a volume setting should never stop the
// app from starting.
namespace settings
{

struct Values
{
    std::string region = "US";
    int         volume = 70;
    bool        muted  = false;
};

Values& get();

void load();
void save();

} // namespace settings
