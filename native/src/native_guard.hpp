#pragma once
#include "native_entries.hpp"
inline bool verifyNativeEntries(){
    for(const auto& entry:nativeEntries)if(memcmp(reinterpret_cast<const void*>(entry.address),entry.bytes,8)){log("native entry mismatch at %08X",unsigned(entry.address));return false;}
    return true;
}
