#pragma once
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace euphoria {
enum WeaponResponseKind { LegacyPush=0, PistolPush, HeavyPistolPush, SmgPush, RiflePush, SniperPush, ShotgunPush, AutoShotgunPush, WeaponResponseCount };
struct WeaponResponse { float impulse, duration, bulletSpeed; };
// Read only the local Xbox 360 export. No proprietary values are embedded.
// weaponinfo force is momentum (N*s), independently of BO1 damage.
struct LocalWeaponResponses {
    WeaponResponse values[WeaponResponseCount] = {};
    LocalWeaponResponses() {
        const char* data=std::getenv("BO1_SDK_DATA");
        if(!data)return;
        std::ifstream input(std::string(data)+"/weaponinfo.xml");
        if(!input) return;
        std::string xml((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        const char* names[WeaponResponseCount]={nullptr,"PISTOL","DEAGLE","MICRO_UZI","AK47","SNIPERRIFLE","SHOTGUN","BARETTA"};
        for(int i=1;i<WeaponResponseCount;++i) {
            const auto begin=xml.find(std::string("<weapon type=\"")+names[i]+"\"");
            if(begin==std::string::npos)continue;
            const auto end=xml.find("</weapon>",begin), physics=xml.find("<physics ",begin);
            if(end==std::string::npos||physics==std::string::npos||physics>=end)continue;
            const auto tagEnd=xml.find('>',physics), force=xml.find("force=\"",physics);
            if(force==std::string::npos||force>=tagEnd)continue;
            char* after=nullptr;const char* number=xml.c_str()+force+7;
            float j=std::strtof(number,&after);
            if(after==number||*after!='"'||!std::isfinite(j)||j<=0||j>128)continue;
            // large-steal-car-4-main gta4-viewer/src/weapons.rs: BULLET_TIME=0.3;
            // shotguns apply an instantaneous impulse (82621E00).
            values[i]={j,(i==ShotgunPush||i==AutoShotgunPush)?0.f:.3f,75.f};
        }
    }
    bool get(unsigned kind,WeaponResponse& out)const {
        if(!kind||kind>=WeaponResponseCount||values[kind].impulse<=0)return false;
        out=values[kind];return true;
    }
};
}
