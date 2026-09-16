#pragma once
#include "ModScriptMessageQueue.h"
namespace ModMessages {
inline ScriptMessages::Queue queue;
inline const char* Show(const char*text){std::string formatted;if(!ScriptMessages::Format(text,formatted))return "Expected nonblank plain text, up to4096 bytes, without control codes";return queue.Push(std::move(formatted));}
}
