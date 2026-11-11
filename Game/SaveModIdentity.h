#pragma once
#include "FileRegistry.h"
#include <string>

namespace SaveModIdentity {
// The envelope changes semantics, not the encoded world schema. Path-bearing
// envelopes are rejected, never guessed or silently reassigned to another mod.
inline constexpr char SlotRoot[] = "EIRebuild64.slot.4";
inline bool CurrentEnvelope(const std::string& root) { return root == SlotRoot; }

// MOD/Title and MOD/Author declare the logical package identity in config.reg.
// Neither the installation directory nor a mod update's Version belongs here.
// Length framing makes the pair unambiguous even when either contains ':'.
inline std::string Make(const std::string& title, const std::string& author)
{
    if (title.empty() || title.find('\0') != std::string::npos || author.find('\0') != std::string::npos)
        return {};
    return "EIRebuild64.mod.1:" + std::to_string(title.size()) + ":" + title +
        ":" + std::to_string(author.size()) + ":" + author;
}
inline std::string Read(CFileRegistry& config)
{
    const CString title = config.GetString("MOD", "Title", "");
    const CString author = config.GetString("MOD", "Author", "");
    return Make(std::string(title.GetString(), title.GetLength()),
                std::string(author.GetString(), author.GetLength()));
}
inline bool BelongsTo(const std::string& saved, const std::string& current)
{
    return !current.empty() && saved == current;
}
}
