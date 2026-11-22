#pragma once

// =============================================================================
// TOME REACH - CONFIG FORM TOKENS (distribution model)
// =============================================================================
//
// A form as distribution configs write it (SPID, SkyPatcher, CDF): an editor id,
// "0xID~Plugin.esp" or "Plugin.esp|0xID" (scripts/spellmap/core.py resolve_token).
// Implemented in TomeReachDistr.cpp.

#include "tomereach/TomeReachReader.h"

#include <string>
#include <string_view>

namespace TomeReach
{
    // A config's form: a plugin's form (key) or an editor id (lower case)
    struct FormToken
    {
        Key key = 0;
        std::string editorId;
    };

    FormToken ParseFormToken(std::string_view text, NameTable& names);

    // The token's form in the store; 0 when its editor id is unknown
    [[nodiscard]] Key ResolveToken(const FormToken& token, const SpellStore& store);
}
