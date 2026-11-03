// Read the supported Steam binary's own skeleton and ragdoll definitions.
// This tool never loads the GTA bridge or changes the actor's motion.
#include <windows.h>
#include <MinHook.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <string>
#include <set>
#include <mutex>
#include <atomic>
#include "native_abi.hpp"

namespace fs = std::filesystem;
struct BoneDef {
    char names[2][20]; unsigned ids[2]; float radius; unsigned char mirror,pad[3];
    float percent,mass,friction,buoyancy; int parent,geometry;
};
struct BaseBone {char name[20];unsigned id,parent;int lerp;};
struct JointDef {unsigned char bone,pad[3];int type,count;float axes[3][3],low[3],high[3],friction[3];};
struct RagdollDef {
    unsigned char used,bound,pad[2];int bones;BoneDef bone[14];int bases;BaseBone base[9];
    int joints;JointDef joint[28];int pairs;unsigned char pair[33][2];unsigned char tail[2];
};
static_assert(sizeof(BoneDef)==0x50 && sizeof(JointDef)==0x54 && sizeof(RagdollDef)==0xf08);
static_assert(offsetof(RagdollDef,joints)==0x58c);
static void* (__cdecl* originalSkel)(native::DObj*,uint32_t*);
static fs::path directory;
static std::set<std::string> exported;
static bool definitions;
static std::mutex captureMutex;
static std::atomic<bool> complete{false};
struct Animation {
    const char* name;uint16_t bytes,shorts,ints,randomBytes,randomInts,frames;
    uint8_t loop,delta,leftGrip,streamable;unsigned streamedBytes;
    uint8_t bones[10],notifies,assetType,isDefault,pad[3];
    unsigned randomShorts,indexCount;float fps,frequency,primed,loopEntry;
    uint16_t* names;uint8_t* dataByte;int16_t* dataShort;int* dataInt;
    int16_t* randomShort;uint8_t* randomByte;int* randomInt;void* indices;void* notify;void* deltaPart;
};
static_assert(sizeof(Animation)==0x68 && offsetof(Animation,names)==0x40);
static void str(FILE* f,const char* text){
    fputc('"',f);for(auto* p=text;p&&*p;p++){
        unsigned char c=*p;
        if(c=='"'||c=='\\'){fputc('\\',f);fputc(c,f);}
        else if(c<32)fprintf(f,"\\u%04x",c);else fputc(c,f);
    }fputc('"',f);
}
static void vec(FILE* f,const float* p,int n){
    fputc('[',f);for(int i=0;i<n;i++){if(i)fputc(',',f);fprintf(f,"%.9g",double(p[i]));}fputc(']',f);
}
static void array(FILE* f,const void* data,unsigned bytes){if(bytes&&data)fwrite(data,1,bytes,f);}
static void sampleOriginal(const Animation* parts,float phase,native::Mat* matrices){
    uint8_t mapping[160]{};for(unsigned i=0;i<160;i++)mapping[i]=uint8_t(i);
    const uint8_t* map=mapping;uint32_t bits[5]{};const uint32_t* ignore=bits;
    float weight=1.f;
    // Steam XAnimCalcParts: weight in xmm3, five stack arguments. Verified
    // at the caller 00873EEA..00873F19; do not use a guessed cdecl signature.
    uintptr_t routine=parts->frames<256?0x870e90:0x872590;
    __asm {
        movss xmm3,weight
        push ignore
        push matrices
        push phase
        push map
        push parts
        mov eax,routine
        call eax
        add esp,20
    }
}
static void deltaTrack(FILE* f,const Animation& a,const uint8_t* track,bool rotation){
    if(!track){fputs("null",f);return;}
    unsigned n=*reinterpret_cast<const uint16_t*>(track);
    if(n>a.frames){fputs("null",f);return;}
    const uint8_t* times=track+(rotation?8:32);
    fputs("{\"times\":[",f);
    for(unsigned i=0;i<=n;i++){
        unsigned t=n?(a.frames<256?times[i]:reinterpret_cast<const uint16_t*>(times)[i]):0;
        if(i)fputc(',',f);fprintf(f,"%u",t);
    }
    fputs("],\"values\":[",f);
    for(unsigned i=0;i<=n;i++){
        if(i)fputc(',',f);
        if(rotation){
            auto* q=n?(*reinterpret_cast<const int16_t* const*>(track+4))+2*i:reinterpret_cast<const int16_t*>(track+4);
            float v[4]={0,0,q[0]/32767.f,q[1]/32767.f};vec(f,v,4);
        }else if(!n)vec(f,reinterpret_cast<const float*>(track+4),3);
        else{
            auto* minima=reinterpret_cast<const float*>(track+4);auto* scale=minima+3;
            auto* data=*reinterpret_cast<const uint8_t* const*>(track+28);float v[3]{};
            for(unsigned k=0;k<3;k++){unsigned value=track[2]?data[3*i+k]:reinterpret_cast<const uint16_t*>(data)[3*i+k];v[k]=minima[k]+scale[k]*value;}
            vec(f,v,3);
        }
    }fputs("]}",f);
}
static void dumpAnimation(const Animation& a){
    if(a.bones[9]>160||a.randomShorts>2000000||a.indexCount>2000000||!std::isfinite(a.fps)||a.fps<=0||a.frames>20000)return;
    auto path=directory/L"animations";fs::create_directories(path);
    std::string safe=a.name;for(auto& c:safe)if(!isalnum(static_cast<unsigned char>(c)))c='_';
    FILE* f=nullptr;_wfopen_s(&f,(path/fs::path(safe+".json")).c_str(),L"wb");if(!f)return;
    fputs("{\"format\":\"bo1-xanim-buffers-1\",\"name\":",f);str(f,a.name);
    fprintf(f,",\"frames\":%u,\"fps\":%.9g,\"loop\":%u,\"delta\":%u,\"bone_count\":[",a.frames,a.fps,a.loop,a.delta);
    for(int i=0;i<10;i++){if(i)fputc(',',f);fprintf(f,"%u",a.bones[i]);}
    fprintf(f,"],\"buffer_counts\":[%u,%u,%u,%u,%u,%u,%u],\"names\":[",a.bytes,a.shorts,a.ints,a.randomShorts,a.randomBytes,a.randomInts,a.indexCount);
    for(int i=0;i<a.bones[9];i++){if(i)fputc(',',f);str(f,native::string(a.names[i]));}
    fputs("],\"root_delta\":",f);
    if(a.deltaPart){
        auto* parts=static_cast<const uint8_t* const*>(a.deltaPart);
        fputs("{\"position\":",f);deltaTrack(f,a,parts[0],false);fputs(",\"rotation\":",f);deltaTrack(f,a,parts[1],true);fputc('}',f);
    }else fputs("null",f);
    fputs(",\"oracle\":[",f);
    float phases[]={0.f,.137f,.5f,.999f};
    for(int s=0;s<4;s++){
        native::Mat pose[160]{};sampleOriginal(&a,phases[s],pose);
        if(s)fputc(',',f);fprintf(f,"{\"phase\":%.9g,\"pose\":[",phases[s]);
        for(unsigned i=0;i<a.bones[9];i++){if(i)fputc(',',f);fputc('[',f);vec(f,pose[i].trans,3);fputc(',',f);vec(f,pose[i].quat,4);fputc(']',f);}
        fputs("]}",f);
    }
    fputs("]}\n",f);fclose(f);
    _wfopen_s(&f,(path/fs::path(safe+".bin")).c_str(),L"wb");if(!f)return;
    array(f,a.dataByte,a.bytes);array(f,a.dataShort,2*a.shorts);array(f,a.dataInt,4*a.ints);
    array(f,a.randomShort,2*a.randomShorts);array(f,a.randomByte,a.randomBytes);array(f,a.randomInt,4*a.randomInts);
    array(f,a.indices,a.indexCount*(a.frames<256?1:2));fclose(f);
}
static void dumpDefinitions(){
    if(definitions)return;
    auto* defs=reinterpret_cast<const RagdollDef*>(0x251cbd0);
    if(defs[0].bones<=0||defs[0].bones>14)return;
    FILE* f=nullptr;_wfopen_s(&f,(directory/L"bo1-ragdoll.json").c_str(),L"wb");if(!f)return;
    fprintf(f,"{\"format\":\"bo1-retail-ragdoll-1\",\"source_va\":\"0251CBD0\",\"definitions\":[");
    for(int d=0;d<2;d++){
        auto& r=defs[d];if(d)fputc(',',f);
        if(r.bones<0||r.bones>14||r.joints<0||r.joints>28||r.pairs<0||r.pairs>33){fclose(f);return;}
        fprintf(f,"{\"bones\":[");
        for(int i=0;i<r.bones;i++){
            auto& b=r.bone[i];if(i)fputc(',',f);
            fputs("{\"start\":",f);str(f,b.names[0]);fputs(",\"end\":",f);str(f,b.names[1]);
            fprintf(f,",\"radius\":%.9g,\"percent\":%.9g,\"mass_engine_units\":%.9g,\"friction\":%.9g,\"parent\":%d,\"geometry\":%d}",b.radius,b.percent,b.mass,b.friction,b.parent,b.geometry);
        }
        fputs("],\"joints\":[",f);
        for(int i=0;i<r.joints;i++){
            auto& j=r.joint[i];if(i)fputc(',',f);
            if(j.count<0||j.count>3){fclose(f);return;}
            fprintf(f,"{\"bone\":%u,\"type\":%d,\"axes\":[",j.bone,j.type);
            for(int a=0;a<j.count&&a<3;a++){if(a)fputc(',',f);vec(f,j.axes[a],3);}
            fputs("],\"low\":",f);vec(f,j.low,j.count);fputs(",\"high\":",f);vec(f,j.high,j.count);
            fputs(",\"friction\":",f);vec(f,j.friction,j.count);fputc('}',f);
        }
        fputs("],\"self_collision\":[",f);for(int i=0;i<r.pairs;i++){if(i)fputc(',',f);fprintf(f,"[%u,%u]",r.pair[i][0],r.pair[i][1]);}
        fputs("]}",f);
    }
    fputs("]}\n",f);fclose(f);definitions=true;
}
static void __cdecl animationAsset(void* header,void* context){
    auto* f=static_cast<FILE*>(context);const char* name=*static_cast<const char**>(header);
    if(name&&strstr(name,"zombie")){str(f,name);fputs("\n",f);}
    if(name&&(strstr(name,"zombie_thundergun_getup")||!strcmp(name,"ai_zombie_idle_v1")||!strcmp(name,"ai_zombie_idle_base")||!strcmp(name,"ai_zombie_sprint")))dumpAnimation(*static_cast<const Animation*>(header));
}
static void captureReady(){
    if(exported.empty()||!definitions)return;
    for(auto clip:{L"ai_zombie_idle_base",L"ai_zombie_thundergun_getup",L"ai_zombie_thundergun_getup_a",
        L"ai_zombie_thundergun_getup_b",L"ai_zombie_thundergun_getup_c",L"ai_zombie_thundergun_getup_quick_a",
        L"ai_zombie_thundergun_getup_quick_b",L"ai_zombie_thundergun_getup_quick_c"}){
        for(auto extension:{L".json",L".bin"}){
            auto file=directory/L"animations"/(std::wstring(clip)+extension);
            if(!fs::is_regular_file(file)||!fs::file_size(file))return;
        }
    }
    FILE* marker=nullptr;_wfopen_s(&marker,(directory/L"capture.ready").c_str(),L"wb");
    if(marker){fputs("BO1 skeleton, native ragdoll and eight animation streams captured\n",marker);fclose(marker);complete=true;}
}
static void dumpModel(native::DObj* obj){
    if(!obj->models||!obj->numModels||!obj->skel.mat||obj->numBones>160||!obj->entnum||obj->entnum>1024)return;
    if(!native::isClientObject(obj)||!native::actor(native::entity(obj->entnum-1)))return;
    std::string name=obj->models[0]->name;
    if(exported.contains(name))return;
    std::string safe=name;for(auto& c:safe)if(!isalnum(static_cast<unsigned char>(c)))c='_';
    FILE* f=nullptr;_wfopen_s(&f,(directory/fs::path(safe+".json")).c_str(),L"wb");if(!f)return;
    fputs("{\"format\":\"bo1-retail-skeleton-1\",\"model\":",f);str(f,name.c_str());fputs(",\"bones\":[",f);
    unsigned offset=0;const auto* attachments=reinterpret_cast<const uint8_t*>(obj->models+obj->numModels);
    for(int m=0;m<obj->numModels;m++){
        auto* model=obj->models[m];
        for(unsigned b=0;b<model->numBones;b++){
            unsigned i=offset+b;bool root=b<model->numRootBones;
            int parent=root?(attachments[m]==255?-1:attachments[m]):int(i-model->parents[b-model->numRootBones]);
            float q[4]={0,0,0,1},p[3]={};
            if(!root){auto* source=model->quats+4*(b-model->numRootBones);for(int k=0;k<4;k++)q[k]=source[k]/32767.f;memcpy(p,model->trans+3*(b-model->numRootBones),12);}
            if(i)fputc(',',f);fputs("{\"name\":",f);str(f,native::string(model->boneNames[b]));
            fprintf(f,",\"parent\":%d,\"local_position\":",parent);vec(f,p,3);fputs(",\"local_rotation\":",f);vec(f,q,4);
            fputs(",\"animation_position\":",f);vec(f,obj->skel.mat[i].trans,3);fputs(",\"animation_rotation\":",f);vec(f,obj->skel.mat[i].quat,4);fputc('}',f);
        }offset+=model->numBones;
    }
    fputs("],\"duplicates\":[",f);
    if(obj->duplicates){
        auto* duplicate=reinterpret_cast<const uint8_t*>(native::string(obj->duplicates))+20;
        for(unsigned n=0;n<160&&duplicate[0];n++,duplicate+=2){if(n)fputc(',',f);fprintf(f,"[%u,%u]",duplicate[0]-1,duplicate[1]-1);}
    }
    fputs("]}\n",f);fclose(f);exported.insert(name);
    dumpDefinitions();
    if(exported.size()==1){
        _wfopen_s(&f,(directory/L"bo1-animation-names.jsonl").c_str(),L"wb");
        if(f){reinterpret_cast<void(__cdecl*)(int,void(__cdecl*)(void*,void*),void*,bool)>(0x54c1c0)(4,animationAsset,f,false);fclose(f);}
    }
}
static void* __cdecl skel(native::DObj* obj,uint32_t* bits){
    bool actor=obj&&obj->entnum&&obj->entnum<=1024&&obj->numBones<=160&&native::actor(native::entity(obj->entnum-1));
    uint32_t all[5]{};if(actor)for(unsigned i=0;i<obj->numBones;i++)all[i>>5]|=0x80000000u>>(i&31);
    void* result=originalSkel(obj,actor?all:bits);
    if(actor&&!complete.load()){
        std::lock_guard guard(captureMutex);
        if(!complete.load()){
            dumpModel(obj);captureReady();
            static ULONGLONG retry;
            if(!complete.load()&&!exported.empty()&&GetTickCount64()>=retry){
                retry=GetTickCount64()+1000;
                FILE* names=nullptr;_wfopen_s(&names,(directory/L"bo1-animation-names.jsonl").c_str(),L"wb");
                if(names){reinterpret_cast<void(__cdecl*)(int,void(__cdecl*)(void*,void*),void*,bool)>(0x54c1c0)(4,animationAsset,names,false);fclose(names);}
                dumpDefinitions();captureReady();
            }
        }
    }
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI DeliriumAttach(void*){
    wchar_t dir[32768]{};if(!GetEnvironmentVariableW(L"BO1_DELIRIUM_CAPTURE_DIR",dir,32768))return 0;
    directory=dir;fs::create_directories(directory);
    // Exact retail ABI: verified entry bytes, not the decompiled project's layout.
    const unsigned char entry[]={0x83,0xec,0x68,0x53,0x8b,0x5c,0x24,0x70};
    if(GetModuleHandleW(nullptr)!=reinterpret_cast<HMODULE>(0x400000)||memcmp(reinterpret_cast<void*>(0x564ab0),entry,8))return 0;
    if(MH_Initialize()!=MH_OK)return 0;
    if(MH_CreateHook(reinterpret_cast<void*>(0x564ab0),skel,reinterpret_cast<void**>(&originalSkel))!=MH_OK)return 0;
    return MH_EnableHook(MH_ALL_HOOKS)==MH_OK;
}
BOOL WINAPI DllMain(HINSTANCE, DWORD,void*){return TRUE;}
