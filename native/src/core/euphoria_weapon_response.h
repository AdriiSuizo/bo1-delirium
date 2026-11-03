#pragma once
namespace euphoria {
enum WeaponResponseKind { LegacyPush=0, PistolPush, HeavyPistolPush, SmgPush, RiflePush, SniperPush, ShotgunPush, AutoShotgunPush, WeaponResponseCount };
struct WeaponResponse {float impulse,duration,bulletSpeed;};
// Mod compatibility presets, in N*s and seconds. These preserve the previous
// build's class responses without opening a game XML file. Weapon identity,
// individual pellet hits and damage still come from BO1's native WeaponDef.
// LSC4's shot path applies non-shotgun pushes over 0.3 s and pellets instantly.
struct LocalWeaponResponses {
    inline static constexpr WeaponResponse values[WeaponResponseCount]={
        {0,0,0},{50,.3f,75},{75,.3f,75},{40,.3f,75},
        {75,.3f,75},{100,.3f,75},{15,0,75},{12,0,75}
    };
    bool get(unsigned kind,WeaponResponse& out) const {
        if(!kind||kind>=WeaponResponseCount)return false;
        out=values[kind];return true;
    }
};
}
