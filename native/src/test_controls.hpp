#pragma once
#include <Xinput.h>
#include "mod_install.hpp"

namespace controls {
enum Action { God, Pause, Spawn, Pursuit, ActionCount };
static constexpr const char* actionNames[]={"press","pause_round","spawn_five","pursuit"};
static int actionHandles[ActionCount],hintHandle;
static unsigned held;
static bool hinted;
static unsigned testAction;
static int testNext;
static std::string lastEvent;
static bool regression;
static void (__cdecl* originalLoadScripts)(int);

static constexpr const char* stockScripts[]={
    "maps/_utility.gsc",
    "common_scripts/utility.gsc",
    "maps/_zombiemode_utility.gsc",
    "maps/_zombiemode_score.gsc",
    "maps/_zombiemode.gsc",
};

static void __cdecl findStockScript(void* asset,void* context){
    const auto* name=native::at<const char*>(asset,0);
    auto& found=*static_cast<unsigned*>(context);
    for(unsigned i=0;i<std::size(stockScripts);++i){
        if(name&&strcmp(name,stockScripts[i])==0)found|=1u<<i;
    }
}

static bool stockScriptsReady(){
    const auto* mode=reinterpret_cast<void*(__cdecl*)(const char*)>(0x5ae810)("zombiemode");
    if(!mode||!native::at<bool>(mode,0x18))return false;

    // Query loaded raw files without asking the script compiler to load them.
    constexpr int rawFileAssetType=36;
    unsigned found=0;
    reinterpret_cast<void(__cdecl*)(int,void(__cdecl*)(void*,void*),void*,bool)>(0x54c1c0)(
        rawFileAssetType,findStockScript,&found,false);
    return found==((1u<<std::size(stockScripts))-1u);
}

static void __cdecl loadScripts(int instance){
    originalLoadScripts(instance);
    if(instance)return;
    for(int& handle:actionHandles)handle=0;
    hintHandle=0;held=0;hinted=false;testAction=0;testNext=20000;lastEvent.clear();
    const auto* modDvar=reinterpret_cast<void*(__cdecl*)(const char*)>(0x5ae810)("fs_game");
    const auto* path=modDvar?native::at<const char*>(modDvar,0x18):nullptr;
    const bool selected=path&&(!_stricmp(path,"mods/bo1delirium")||!_stricmp(path,"mods\\bo1delirium"));
    {
        std::lock_guard guard(phys::mutex);
        phys::reset();
        phys::mapFailed=false;
        phys::modSelected=selected;
        clipMap=nullptr;
        lastReport=native::time()-5001;
    }
    if(!selected){
        log("Delirium inactive: select BO1 Delirium in Mods");
        return;
    }
    if(!regression&&!installation::testControls()){
        log("Steam test controls off (installation.ini test_controls=0)");
        return;
    }
    // The frontend also compiles server scripts, but only Zombies maps load
    // the stock scripts referenced by our controls.
    if(!stockScriptsReady()){
        log("Steam test controls skipped: Zombies scripts unavailable");
        return;
    }
    // The function table exists only during script compilation. Resolve our
    // own function now; never use a stale VM handle after a map restart.
    constexpr char script[]="maps/delirium/controls";
    int loaded=reinterpret_cast<int(__cdecl*)(int,const char*)>(0x661af0)(0,script);
    if(loaded)for(unsigned i=0;i<ActionCount;++i)
        actionHandles[i]=reinterpret_cast<int(__cdecl*)(int,const char*,const char*)>(0x4e3470)(0,script,actionNames[i]);
    if(loaded)hintHandle=reinterpret_cast<int(__cdecl*)(int,const char*,const char*)>(0x4e3470)(0,script,"hint");
    log("Steam test controls script loaded %d handle %d",loaded,actionHandles[God]);
}
static bool press(Action action){
    // Script handles are resolved only for a selected Zombies mod/map. The
    // first AI creates the physical world, so controls must work before it.
    if(!actionHandles[action]||!phys::modSelected)return false;
    auto* player=native::entity(0);
    if(!native::at<void*>(player,0x13c)||native::at<int>(player,0x184)<=0)return false;
    auto& flags=native::at<unsigned>(player,0x170);
    if(action==God)flags^=1u;
    if(action==Pursuit)flags^=4u; // Retail SP IsNoTarget reads this bit.
    auto thread=reinterpret_cast<unsigned short(__cdecl*)(int,int,unsigned)>(0x5598e0)(0,actionHandles[action],0);
    reinterpret_cast<void(__cdecl*)(unsigned,int)>(0x5de2c0)(thread,0);
    return true;
}
static unsigned inputs(){
    DWORD owner=0;GetWindowThreadProcessId(GetForegroundWindow(),&owner);
    if(owner!=GetCurrentProcessId())return 0;
    constexpr int keys[]={VK_DOWN,VK_UP,VK_LEFT,VK_RIGHT};
    constexpr WORD pad[]={XINPUT_GAMEPAD_DPAD_DOWN,XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_RIGHT};
    unsigned mask=0;
    for(unsigned i=0;i<ActionCount;++i)if(GetAsyncKeyState(keys[i])&0x8000)mask|=1u<<i;
    using State=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
    static State get=[](){
        for(auto name:{L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"})
            if(auto dll=LoadLibraryW(name))if(auto p=GetProcAddress(dll,"XInputGetState"))return reinterpret_cast<State>(p);
        return State{};
    }();
    if(get)for(DWORD i=0;i<4;i++){
        XINPUT_STATE s{};
        if(get(i,&s)==ERROR_SUCCESS)for(unsigned j=0;j<ActionCount;++j)
            if(s.Gamepad.wButtons&pad[j])mask|=1u<<j;
    }
    return mask;
}
static void tick(){
    if(!hinted&&hintHandle&&phys::modSelected&&native::at<void*>(native::entity(0),0x13c)&&native::at<int>(native::entity(0),0x184)>0){
        auto thread=reinterpret_cast<unsigned short(__cdecl*)(int,int,unsigned)>(0x5598e0)(0,hintHandle,0);
        reinterpret_cast<void(__cdecl*)(unsigned,int)>(0x5de2c0)(thread,0);
        hinted=true;log("Steam test controls hint shown");
    }
    const unsigned mask=inputs(), edges=mask&~held;
    for(unsigned i=0;i<ActionCount;++i)if(edges&(1u<<i))press(static_cast<Action>(i));
    held=mask;
    if(testEnabled&&phys::modSelected){
        auto* event=reinterpret_cast<void*(__cdecl*)(const char*)>(0x5ae810)("delirium_control_event");
        const auto* message=event?native::at<const char*>(event,0x18):nullptr;
        if(message&&lastEvent!=message){
            lastEvent=message;
            log("STEAM CONTROLS RESULT %s",message);
        }
    }
    if(regression&&phys::modSelected){
        constexpr Action sequence[]={God,Pause,Spawn,Pursuit,Pursuit,Pause,God};
        if(testAction<std::size(sequence)&&native::time()>=testNext&&press(sequence[testAction])){
            log("STEAM CONTROLS TEST action %u",testAction);
            testNext=native::time()+(testAction==0?20000:4000);
            ++testAction;
        }
    }
}
}
