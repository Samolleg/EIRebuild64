#pragma once

namespace owned {

struct RendererOption;
struct PlayerGraphicsSetting;

// Language of the renderer's own menus (F10 settings, the developer window,
// the HUD layout editor and combat UI settings). Translation happens at draw
// time from English source text, so option keys, groups and saved values stay
// English and a language switch applies on the next frame. A string without
// a translation draws in English.
//
// ui_language option: 0 automatic (Russian when the Windows display language
// is Russian or the ANSI code page is 1251), 1 English, 2 Russian.
void set_ui_language(int setting) noexcept;
// Choices of the ui_language option; each language keeps its own name.
inline constexpr const char kUiLanguageChoices[] = "Auto\0" "English\0" u8"\u0420\u0443\u0441\u0441\u043a\u0438\u0439\0";
bool ui_russian() noexcept;

// Free text, looked up by its exact English source (format strings included;
// a translation keeps the same printf conversions in the same order).
const char* ui_text(const char* english) noexcept;
// A '\0'-separated choice list (as passed to ImGui::Combo), looked up whole.
const char* ui_choices(const char* english) noexcept;

const char* ui_option_label(const RendererOption& option) noexcept;
const char* ui_option_description(const RendererOption& option) noexcept;
const char* ui_setting_label(const PlayerGraphicsSetting& setting) noexcept;
const char* ui_setting_tooltip(const PlayerGraphicsSetting& setting) noexcept;

// Code page of the game's own text bytes. game.exe passes its "Font CharSet"
// setting (HKCU <game root>\Game Specific Settings, default "english") to the
// CRT as setlocale(LC_CTYPE, name). "russian" (any case) means 1251; any other
// value, or a missing one, keeps the Windows ANSI code page (CP_ACP), which is
// what the Russian release relies on with "english" on a 1251 system.
// Read at the first DirectDraw creation that knows the registry root, then
// fixed for the run; CP_ACP until then.
void read_game_text_code_page(const wchar_t* registry_root) noexcept;
unsigned game_text_code_page() noexcept;

} // namespace owned
