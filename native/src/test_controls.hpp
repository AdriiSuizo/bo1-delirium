#pragma once
#include <Xinput.h>

namespace controls {
static int pressHandle,hintHandle;
static bool held,hinted;
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
    pressHandle=0;hintHandle=0;held=false;hinted=false;
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
    if(loaded)pressHandle=reinterpret_cast<int(__cdecl*)(int,const char*,const char*)>(0x4e3470)(0,script,"press");
    if(loaded)hintHandle=reinterpret_cast<int(__cdecl*)(int,const char*,const char*)>(0x4e3470)(0,script,"hint");
    log("Steam test controls script loaded %d handle %d",loaded,pressHandle);
}
static bool press(){
    if(!pressHandle||!phys::g_world||strncmp(phys::mapName.c_str(),"maps/zombie_",12))return false;
    auto* player=native::entity(0);
    if(!native::at<void*>(player,0x13c)||native::at<int>(player,0x184)<=0)return false;
    native::at<unsigned>(player,0x170)^=1u;
    auto thread=reinterpret_cast<unsigned short(__cdecl*)(int,int,unsigned)>(0x5598e0)(0,pressHandle,0);
    reinterpret_cast<void(__cdecl*)(unsigned,int)>(0x5de2c0)(thread,0);
    log("Steam test controls pressed god %u",native::at<unsigned>(player,0x170)&1u);
    if(testEnabled){
        auto* event=reinterpret_cast<void*(__cdecl*)(const char*)>(0x5ae810)("delirium_control_event");
        if(event)log("STEAM CONTROLS RESULT %s",native::at<const char*>(event,0x18));
    }
    return true;
}
static bool inputDown(){
    DWORD owner=0;GetWindowThreadProcessId(GetForegroundWindow(),&owner);
    if(owner!=GetCurrentProcessId())return false;
    bool down=(GetAsyncKeyState(VK_DOWN)&0x8000)!=0;
    using State=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
    static State get=[](){
        for(auto name:{L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"})
            if(auto dll=LoadLibraryW(name))if(auto p=GetProcAddress(dll,"XInputGetState"))return reinterpret_cast<State>(p);
        return State{};
    }();
    if(get)for(DWORD i=0;i<4;i++){XINPUT_STATE s{};if(get(i,&s)==ERROR_SUCCESS&&(s.Gamepad.wButtons&XINPUT_GAMEPAD_DPAD_DOWN))down=true;}
    return down;
}
static void tick(){
    if(!hinted&&hintHandle&&phys::g_world&&native::at<void*>(native::entity(0),0x13c)&&native::at<int>(native::entity(0),0x184)>0){
        auto thread=reinterpret_cast<unsigned short(__cdecl*)(int,int,unsigned)>(0x5598e0)(0,hintHandle,0);
        reinterpret_cast<void(__cdecl*)(unsigned,int)>(0x5de2c0)(thread,0);
        hinted=true;log("Steam test controls hint shown");
    }
    bool down=inputDown();if(down&&!held)press();held=down;
    if(regression&&phys::g_world){
        static int count=0,next=20000;
        if(count<3&&native::time()>=next&&press()){
            count++;next=native::time()+6000;log("STEAM CONTROLS TEST press %d",count);
        }
    }
}
}
