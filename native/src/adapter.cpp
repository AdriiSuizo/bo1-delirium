#include <windows.h>
#include <MinHook.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include "native_abi.hpp"
#include <share.h>

static HMODULE module;
static void (__cdecl *originalInit)(char*);
static FILE* logFile;
static void (__cdecl *originalThink)(void*);
static void* (__cdecl *originalSkel)(native::DObj*,uint32_t*);
static void (__cdecl *originalPain)(void*,void*,int,const float*,int,const float*,int,int);
static void (__cdecl *originalDie)(void*,void*,void*,int,int,int,const float*,int,int);
static void (__cdecl *originalAnimationTime)(void*,int,void(__cdecl*)());
static void (__cdecl *originalRunThink)(void*);
static void (__cdecl *originalFree)(void*);
static void (__cdecl *originalGib)(int,void*,unsigned);
static void (__cdecl *originalRunFrame)(int);
using Damage=void(__cdecl*)(void*,void*,void*,const float*,const float*,int,int,int,int,int,int,unsigned);
static Damage originalDamage;
struct DamageEvent {void* entity;void* inflictor;const float* point;};
static thread_local const DamageEvent* damageEvent;
static bool testEnabled;
static int lastReport;
static void* clipMap;
static void log(const char* format,...){
    if(!logFile)return;
    fprintf(logFile,"%llu ",GetTickCount64());va_list args;va_start(args,format);vfprintf(logFile,format,args);va_end(args);
    fputc('\n',logFile);if(testEnabled)fflush(logFile);
}
#include "physics_bridge.hpp"
#include "world_collision.hpp"
#include "native_guard.hpp"
#include "test_controls.hpp"
static void selftest(){
    if(!testEnabled||controls::regression||!phys::g_world)return;
    auto* player=native::entity(0);native::at<unsigned>(player,0x170)|=1u;
    static int target=-1,shots=0,nextShot,report,stage=0,stageTime;
    if(target<0){
        for(int index=0;index<1024;index++){
            auto& a=phys::actors[index];void* ent=native::entity(index);
            if(!a.bound||!native::actor(ent)||native::at<int>(ent,0x184)<=0||a.active)continue;
            auto* owner=native::at<void*>(ent,0x140);
            float distance=phys::length(phys::origin(ent)-phys::origin(player));
            if(distance<140||distance>450||native::at<int>(owner,0xde0)!=1)continue;
            target=index;native::at<int>(ent,0x184)=1200;native::at<int>(ent,0x188)=1200;
            nextShot=native::time()+500;log("STEAM TEST selected native pursuer entity %d distance %.2f",index,distance);break;
        }
    }
    if(target<0)return;
    void* ent=native::entity(target);auto& a=phys::actors[target];
    // Opt-in intact-survivor regression. Normal test/gameplay paths are unchanged.
    static const bool blastOnly=[](){wchar_t value[8]{};return GetEnvironmentVariableW(L"BO1_STEAM_BLAST_TEST",value,8)&&value[0]==L'1';}();
    if(blastOnly){
        if(shots<3&&native::time()>=nextShot&&native::actor(ent)&&(!shots||!a.active)){
            auto point=a.animation[a.bone[8]],dir=phys::normalize(point-(phys::origin(player)+phys::v3(0,0,55)));
            phys::hit(ent,20,&point.x,4,&dir.x,0,0,false);
            log("STEAM BLAST TEST intact adapter explosion %d entity %d health %d detached %04X",shots,target,native::at<int>(ent,0x184),a.detachedMask);
            ++shots;nextShot=native::time()+12000;
        }
        if(native::time()>=report){report=native::time()+500;log("STEAM BLAST TEST state entity %d active %d flags %u detached %04X health %d blasts %d",target,a.active,a.status.flags,a.detachedMask,native::at<int>(ent,0x184),shots);}
        return;
    }
    if(shots<3&&native::time()>=nextShot&&native::actor(ent)){
        auto point=a.animation[a.bone[8]],dir=phys::normalize(point-(phys::origin(player)+phys::v3(0,0,55)));
        int weapon=native::at<unsigned char>(player,0xd4);int health=native::at<int>(ent,0x184);
        reinterpret_cast<Damage>(0x5405c0)(ent,player,player,&dir.x,&point.x,20,0,1,weapon,4,0,0);
        log("STEAM TEST real G_Damage entity %d shot %d health %d -> %d weapon %d",target,shots,health,native::at<int>(ent,0x184),weapon);
        shots++;nextShot=native::time()+150;
        if(shots==3)stageTime=native::time()+5000;
    }
    if(shots==3&&stage==0&&native::time()>=stageTime&&native::actor(ent)){
        unsigned shotgun=0,count=*reinterpret_cast<unsigned*>(0xbe1fb0);
        for(unsigned i=1;i<=count&&i<2048;i++)if(phys::profile(i)==6){shotgun=i;break;}
        if(shotgun){
            auto point=a.animation[a.bone[2]],dir=phys::normalize(point-(phys::origin(player)+phys::v3(0,0,55)));
            for(int i=0;i<8;i++)reinterpret_cast<Damage>(0x5405c0)(ent,player,player,&dir.x,&point.x,10,0,1,shotgun,15,0,0);
            log("STEAM TEST native shotgun pellets entity %d weapon %u health %d",target,shotgun,native::at<int>(ent,0x184));
        }else log("STEAM TEST no loaded pump shotgun; skipping pellet case");
        stage=1;stageTime=native::time()+7000;
    }
    if(stage==1&&native::time()>=stageTime&&native::actor(ent)){
        auto point=a.animation[a.bone[8]],dir=phys::normalize(point-(phys::origin(player)+phys::v3(0,0,55)));
        reinterpret_cast<Damage>(0x5405c0)(ent,player,player,&dir.x,&point.x,75,0,4,0,0,0,0);
        log("STEAM TEST native explosion entity %d health %d",target,native::at<int>(ent,0x184));stage=2;stageTime=native::time()+18000;
    }
    if(stage==2&&native::time()>=stageTime&&native::actor(ent)){
        auto point=a.animation[a.bone[8]],dir=phys::normalize(point-(phys::origin(player)+phys::v3(0,0,55)));
        reinterpret_cast<Damage>(0x5405c0)(ent,player,player,&dir.x,&point.x,5000,0,1,native::at<unsigned char>(player,0xd4),4,0,0);
        log("STEAM TEST lethal native hit entity %d health %d type %u",target,native::at<int>(ent,0x184),native::at<uint16_t>(ent,0xbe));stage=3;
    }
    if(shots&&native::time()>=report){report=native::time()+500;log("STEAM TEST state entity %d active %d frozen %d flags %u health %d type %u origin %.2f %.2f %.2f",target,a.active,a.frozen,a.status.flags,native::at<int>(ent,0x184),native::at<uint16_t>(ent,0xbe),phys::origin(ent).x,phys::origin(ent).y,phys::origin(ent).z);}
}
static void __cdecl asset(void* header,void*){
    auto* cm=static_cast<native::ClipMap*>(header);
    log("native collision candidate %p name %s nodes %u leafs %u triangles %d vertices %u brushes %u",header,cm->name,cm->nodeCount,cm->leafCount,cm->triangleCount,cm->vertexCount,cm->brushCount);
    if(cm->inUse&&cm->nodeCount>0&&cm->nodeCount<1000000&&cm->triangleCount>0&&cm->triangleCount<1000000)clipMap=header;
}
static void __cdecl think(void* ent){
    auto before=phys::origin(ent);
    originalThink(ent);
    if(native::time()-lastReport>5000){
        lastReport=native::time();
        if(logFile)fflush(logFile);
        auto& p=native::at<float[3]>(ent,0x11c);
        log("native actor tick t %d entity %d origin %.2f %.2f %.2f health %d",lastReport,native::at<int>(ent,0),p[0],p[1],p[2],native::at<int>(ent,0x184));
        if(!clipMap){auto enumerate=reinterpret_cast<void(__cdecl*)(int,void(__cdecl*)(void*,void*),void*,bool)>(0x54c1c0);enumerate(11,asset,nullptr,false);enumerate(12,asset,nullptr,false);}
    }
    if(clipMap){std::lock_guard guard(phys::mutex);phys::world(static_cast<native::ClipMap*>(clipMap));}
    phys::afterThink(ent,before);
    selftest();
}
static void* __cdecl skeleton(native::DObj* obj,uint32_t* bits){
    uint32_t all[5]{};
    const bool actor=phys::g_world&&obj->entnum>0&&obj->entnum<=1024&&obj->numBones<=160&&(native::humanoidActor(native::entity(obj->entnum-1))||phys::actors[obj->entnum-1].active||phys::actors[obj->entnum-1].frozen);
    if(actor)for(int i=0;i<obj->numBones;i++)all[i>>5]|=0x80000000u>>(i&31);
    void* result=originalSkel(obj,actor?all:bits);
    if(actor&&obj->localClient!=255){
        static unsigned samples;
        if(samples++<4)log("native skin entity %u client %u bones %u models %u model %s root %.2f %.2f %.2f",obj->entnum-1,obj->localClient,obj->numBones,obj->numModels,obj->models[0]->name,obj->skel.mat[0].trans[0],obj->skel.mat[0].trans[1],obj->skel.mat[0].trans[2]);
    }
    phys::pose(obj);
    return result;
}
static void __cdecl pain(void* ent,void* attacker,int damage,const float* point,int mod,const float* dir,int hitloc,int weapon){
    if(testEnabled)log("native wound entity %d damage %d mod %d hitloc %d weapon %d",native::at<int>(ent,0),damage,mod,hitloc,weapon);
    bool event=damageEvent&&damageEvent->entity==ent;
    auto source=event&&damageEvent->inflictor?phys::origin(damageEvent->inflictor):phys::origin(ent);
    phys::hit(ent,damage,point,mod,dir,hitloc,weapon,false,event?&source.x:nullptr);
    // Preserve native damage metadata, attacker memory and scripts, while
    // giving the physical bridge the pain response instead of AIS_PAIN.
    void* owner=native::at<void*>(ent,0x140);int index=native::at<int>(ent,0);
    bool physical=phys::g_world&&index>=0&&index<1024&&phys::actors[index].bound
        &&native::time()-phys::actors[index].sampleTime<250&&(mod==1||mod==2||mod==9);
    unsigned char allowed=owner?native::at<unsigned char>(owner,0xc60):0;
    if(physical&&owner)native::at<unsigned char>(owner,0xc60)=0;
    originalPain(ent,attacker,damage,point,mod,dir,hitloc,weapon);
    if(physical&&owner)native::at<unsigned char>(owner,0xc60)=allowed;
}
static void __cdecl die(void* ent,void* inflictor,void* attacker,int damage,int mod,int weapon,const float* dir,int hitloc,int offset){
    int index=native::at<int>(ent,0);if(index>=0&&index<1024){
        auto& a=phys::actors[index];auto point=a.bound?a.animation[a.bone[8]]:phys::origin(ent);
        bool event=damageEvent&&damageEvent->entity==ent;
        auto source=event&&damageEvent->inflictor?phys::origin(damageEvent->inflictor):phys::origin(ent);
        phys::hit(ent,damage,event&&damageEvent->point?damageEvent->point:&point.x,mod,dir,hitloc,weapon,true,event?&source.x:nullptr);
    }
    originalDie(ent,inflictor,attacker,damage,mod,weapon,dir,hitloc,offset);
}
static void __cdecl damage(void* ent,void* inflictor,void* attacker,const float* dir,const float* point,int amount,int flags,int mod,int weapon,int hitloc,int offset,unsigned model){
    DamageEvent event{ent,inflictor,point};const auto* previous=damageEvent;damageEvent=&event;
    originalDamage(ent,inflictor,attacker,dir,point,amount,flags,mod,weapon,hitloc,offset,model);
    damageEvent=previous;
}
static void __cdecl animationTime(void* ent,int notify,void(__cdecl* callback)()){
    phys::updateRun(ent);originalAnimationTime(ent,notify,callback);
}
static void __cdecl runThink(void* ent){originalRunThink(ent);phys::corpseRoot(ent);}
static void __cdecl runFrame(int time){originalRunFrame(time);phys::postFrame();controls::tick();}
static void __cdecl freeEntity(void* ent){phys::freed(ent);originalFree(ent);}
static void __cdecl gib(int client,void* cent,unsigned parameter){
    if(client==0)phys::dismember(native::at<int>(cent,0x164),parameter);
    originalGib(client,cent,parameter);
}
static void __cdecl initialized(char* commandLine){
    log("native Com_Init enter");originalInit(commandLine);log("native Com_Init returned: main-thread game API ready");
}
extern "C" __declspec(dllexport) DWORD WINAPI DeliriumAttach(void*){
    wchar_t root[MAX_PATH]{};GetEnvironmentVariableW(L"BO1_STEAM_DELIRIUM_ROOT",root,MAX_PATH);
    wchar_t logs[32768]{};GetEnvironmentVariableW(L"BO1_DELIRIUM_LOG_DIR",logs,32768);
    logFile=_wfsopen((std::filesystem::path(logs)/L"native-runtime.log").c_str(),L"w",_SH_DENYNO);
    log("Delirium native adapter loaded, module %p",module);
    wchar_t test[8]{};testEnabled=GetEnvironmentVariableW(L"BO1_STEAM_DELIRIUM_TEST",test,8)&&test[0]==L'1';
    controls::regression=testEnabled&&GetEnvironmentVariableW(L"BO1_STEAM_CONTROLS_TEST",test,8)&&test[0]==L'1';
    if(GetModuleHandleW(nullptr)!=reinterpret_cast<HMODULE>(0x400000)){log("unexpected executable image base");return 0;}
    if(std::strcmp(reinterpret_cast<const char*>(0xB61624),"C:\\projects_pc\\cod\\codsrc\\src\\obj\\t5\\CoDSteam_CEG_bin\\BlackOps.pdb")){
        log("unsupported original binary identity");return 0;
    }
    if(!verifyNativeEntries())return 0;
    if(MH_Initialize()!=MH_OK){log("MinHook initialization failed");return 0;}
    auto result=MH_CreateHook(reinterpret_cast<void*>(0x4069C0),&initialized,reinterpret_cast<void**>(&originalInit));
    if(result!=MH_OK){log("Com_Init hook refused: %d",result);return 0;}
    if(MH_CreateHook(reinterpret_cast<void*>(0x49e280),&think,reinterpret_cast<void**>(&originalThink))!=MH_OK)return 0;
    if(MH_CreateHook(reinterpret_cast<void*>(0x564ab0),&skeleton,reinterpret_cast<void**>(&originalSkel))!=MH_OK)return 0;
    if(MH_CreateHook(reinterpret_cast<void*>(0x431ac0),&pain,reinterpret_cast<void**>(&originalPain))!=MH_OK)return 0;
    if(MH_CreateHook(reinterpret_cast<void*>(0x50bf00),&die,reinterpret_cast<void**>(&originalDie))!=MH_OK)return 0;
    if(MH_CreateHook(reinterpret_cast<void*>(0x60c640),&animationTime,reinterpret_cast<void**>(&originalAnimationTime))!=MH_OK)return 0;
    if(MH_CreateHook(reinterpret_cast<void*>(0x5405c0),&damage,reinterpret_cast<void**>(&originalDamage))!=MH_OK)return 0;
    if(MH_CreateHook(reinterpret_cast<void*>(0x5164d0),&runThink,reinterpret_cast<void**>(&originalRunThink))!=MH_OK)return 0;
    if(MH_CreateHook(reinterpret_cast<void*>(0x5025e0),&freeEntity,reinterpret_cast<void**>(&originalFree))!=MH_OK)return 0;
    if(MH_CreateHook(reinterpret_cast<void*>(0x593e40),&gib,reinterpret_cast<void**>(&originalGib))!=MH_OK)return 0;
    if(MH_CreateHook(reinterpret_cast<void*>(0x45a6c0),&runFrame,reinterpret_cast<void**>(&originalRunFrame))!=MH_OK)return 0;
    if(MH_CreateHook(reinterpret_cast<void*>(0x6124b0),&controls::loadScripts,reinterpret_cast<void**>(&controls::originalLoadScripts))!=MH_OK)return 0;
    result=MH_EnableHook(MH_ALL_HOOKS);log("Com_Init hook installation %d",result);
    return result==MH_OK;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void*){
    if(reason==DLL_PROCESS_ATTACH){module=instance;DisableThreadLibraryCalls(instance);}return TRUE;
}
