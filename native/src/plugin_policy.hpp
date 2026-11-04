#pragma once
#include <span>
#include <string_view>

namespace pluginPolicy {
// The game argument and -lan switch are supplied by the normal Plutonium
// launcher. Do not enable this native adapter in an online or dedicated host.
inline bool permitted(std::span<const std::wstring_view> arguments){
    if(arguments.size()<4||arguments[1]!=L"t5sp"||arguments[2].empty())return false;
    bool lan=false;
    for(size_t i=3;i<arguments.size();++i){
        if(arguments[i]==L"-dedicated"||arguments[i]==L"-dedicatedlan")return false;
        if(arguments[i]==L"-lan")lan=true;
    }
    return lan;
}
}
