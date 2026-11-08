#pragma once
#include "ScriptLanguage.h"
#include "ModRuntime.h"
// Resolve at each game entry point: global CScript objects predate the Session.
inline ScriptLanguage CurrentScriptLanguage() {
    return ModRuntime::Current() ? ScriptLanguage::Supported() : ScriptLanguage::Legacy();
}
