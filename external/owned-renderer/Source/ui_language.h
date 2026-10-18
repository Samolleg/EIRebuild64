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

} // namespace owned
