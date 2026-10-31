#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace MapResources {
inline constexpr const char *Directory = "Maps64";
inline constexpr const wchar_t *WideDirectory = L"Maps64";

// The engine still produces a few logical "maps/" references. Both spellings
// name the same x64 resource; neither is a second physical search directory.
inline std::string Key(std::string reference)
{
    std::replace(reference.begin(),reference.end(),'\\','/');
    std::string folded=reference;
    std::transform(folded.begin(),folded.end(),folded.begin(),[](unsigned char c){return char(std::tolower(c));});
    const auto modern=folded.rfind("/maps64/"),legacy=folded.rfind("/maps/");
    if(modern!=std::string::npos)reference.erase(0,modern+8);
    else if(legacy!=std::string::npos)reference.erase(0,legacy+6);
    else if(folded.rfind("maps64/",0)==0)reference.erase(0,7);
    else if(folded.rfind("maps/",0)==0)reference.erase(0,5);
    const std::filesystem::path path(reference);
    if(reference.empty() || reference.find(':')!=std::string::npos || path.is_absolute() || path.has_root_path())
        throw std::runtime_error("Map resource must be relative to Maps64");
    for(const auto &part:path)if(part==".." || part==".")
        throw std::runtime_error("Map resource cannot leave Maps64");
    std::replace(reference.begin(),reference.end(),'/','\\');
    std::transform(reference.begin(),reference.end(),reference.begin(),[](unsigned char c){return char(std::tolower(c));});
    return reference;
}
}
