#pragma once
#include "native_abi.hpp"
#include "core/ragdoll.h"
#include "core/euphoria_math.h"
#include "core/euphoria_retarget.h"
#include "core/euphoria_animation_return.h"
#include "core/euphoria_run_resume.h"
#include "core/euphoria_wound_budget.h"
#include "core/euphoria_hit_queue.h"
#include "core/brush_faces.h"
#include "core/euphoria_crowd.h"
#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <mutex>
#include <chrono>
#include "damage_routing.hpp"

namespace phys {
using namespace euphoria;
static std::recursive_mutex mutex;
struct Api {
#define FN(name) decltype(&rd_##name) name{}
    FN(world_create_ex);FN(world_destroy);FN(load_art);FN(model_rest_bodies);FN(model_rest_segments);
    FN(add_static_mesh);FN(add_static_convex);FN(actor_spawn);FN(actor_despawn);FN(actor_set_mode);
    FN(actor_set_pose_rotations);FN(actor_set_bodies);FN(actor_set_motion);FN(actor_get_bodies);
    FN(actor_get_joint_positions);FN(actor_get_state);FN(world_update);FN(actor_hit);
#undef FN
    int (__cdecl* actor_get_render_bodies)(const RdWorld*,unsigned,RdTransform*,unsigned){};
    int (__cdecl* actor_get_body_velocities)(const RdWorld*,unsigned,RdVec3*,RdVec3*){};
    int (__cdecl* actor_hit_profile)(RdWorld*,unsigned,const RdHit*,float,float,unsigned){};
    int (__cdecl* weapon_response)(unsigned,float*,float*,float*){};
    int (__cdecl* world_set_async)(RdWorld*,unsigned){};
    int (__cdecl* actor_get_bounds)(const RdWorld*,unsigned,RdVec3*,RdVec3*){};
    int (__cdecl* actor_get_box_vertices)(const RdWorld*,unsigned,unsigned,RdVec3*){};
    int (__cdecl* actor_set_detached_parts)(RdWorld*,unsigned,unsigned){};
    int (__cdecl* actor_can_activate)(RdWorld*,unsigned){};
    int (__cdecl* actor_push)(RdWorld*,unsigned,unsigned,RdVec3,float,unsigned){};
    int (__cdecl* model_body_masses)(const RdWorld*,float*){};
    int (__cdecl* actor_set_gravity)(RdWorld*,unsigned,float){};
};
static Api g_api;
static RdWorld* g_world;
static bool modSelected;
static unsigned model;
static native::ClipMap* map;
static std::string mapName;
static Quat bodyOffset[16];
static RdTransform rest[16];
static int lastLevelTime;
static uint64_t lastStep;
static bool mapFailed;
static int lastRenderStamp=-1;
static uint64_t millis(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
static Vec3 v(RdVec3 a){return {a.x,a.y,a.z};}
static RdVec3 r(Vec3 a){return {a.x,a.y,a.z};}
static Quat q(RdQuat a){return {a.x,a.y,a.z,a.w};}
static RdQuat r(Quat a){return {a.x,a.y,a.z,a.w};}
static Vec3 pos(const native::Mat& a){return {a.trans[0],a.trans[1],a.trans[2]};}
static Quat rot(const native::Mat& a){return qnormalize({a.quat[0],a.quat[1],a.quat[2],a.quat[3]});}
static Vec3 origin(void* ent){return native::at<Vec3>(ent,0x11c);}
struct BoneDesc {const char* name;const char* tip;bool arm;};
static constexpr BoneDesc bones[16]={
    {"pelvis|j_mainroot","j_spinelower",false},{"j_hip_le","j_knee_le",false},
    {"j_knee_le","j_ankle_le",false},{"j_ankle_le","j_ball_le",false},
    {"j_hip_ri","j_knee_ri",false},{"j_knee_ri","j_ankle_ri",false},
    {"j_ankle_ri","j_ball_ri",false},{"j_spinelower","j_spine4|j_spineupper",false},
    {"j_spine4|j_spineupper","j_neck",false},{"j_neck","j_head",false},
    {"j_shoulder_le","j_elbow_le",true},{"j_elbow_le","j_wrist_le",true},
    {"j_wrist_le","j_mid_le_1|j_index_le_1",true},{"j_shoulder_ri","j_elbow_ri",true},
    {"j_elbow_ri","j_wrist_ri",true},{"j_wrist_ri","j_mid_ri_1|j_index_ri_1",true}};
static Quat anatomical(Vec3 along,bool arm){
    Vec3 x=normalize(along);Vec3 refs[]={arm?v3(1,0,0):v3(0,1,0),arm?v3(0,1,0):v3(1,0,0),v3(0,0,1)};
    for(Vec3 ref:refs){Vec3 y=ref-x*dot(ref,x);if(lengthSq(y)>.04f){y=normalize(y);return qnormalize(qFromAxes(x,y,cross(x,y)));}}
    return qIdentity();
}
struct Actor {
    void* owner{};bool bound{},active{},dead{},returning{},frozen{};unsigned handle{},skin{},detachedMask{};
    int count{},sampleTime{},activeSince{},balancedSince{},lastImpact{},lastReport{},capturedStamp{-1};
    short parent[160]{},body[160]{},duplicate[160]{};unsigned char bone[16]{};bool valid[16]{};
    Quat offset[16]{};Vec3 attachment[160]{},animation[160]{},draw[160]{},previous[160]{};
    Quat animationQ[160]{},drawQ[160]{},previousQ[160]{};
    Vec3 velocity{},desired{},pelvisOffset{},previousOrigin{},rootVelocity{},linear[16]{},angular[16]{};uint64_t sampleStamp{};
    Vec3 incoming{},incomingLinear[16]{},incomingAngular[16]{},blast{};int incomingTime{};
    int deadSince{},settledSince{},contactTime{},pushTime{};bool pendingContact{},readyRelease{};float pushBudget{};
    float nominalSpeed{},runScale{1.f},gravityScale{1.f};
    // Recent supported COM velocities, sampled once per server tick.
    Vec3 supportedVelocity[4]{};
    unsigned supportSamples{};
    int supportTime{};
    struct Rate {void* tree{};uint16_t info{};float base{},applied{};bool owned{};} rate[2];
    RdActorState status{};AnimationReturn blend{};RunResume run{};WoundBudget wounds{};BulletQueue hits{},weakPellets{};
};
static std::array<Actor,1024> actors;
static bool present(const Actor& actor,int part){return actor.valid[part]&&!(actor.detachedMask&(1u<<part));}
static bool alias(const char* names,const char* name){
    if(!name)return false;while(*names){const char* end=strchr(names,'|');size_t n=end?size_t(end-names):strlen(names);
        if(strlen(name)==n&&!strncmp(names,name,n))return true;if(!end)break;names=end+1;}return false;
}
static unsigned skin(const native::DObj* obj){
    unsigned h=2166136261u;h=(h^obj->numBones)*16777619u;
    for(int i=0;i<obj->numModels;i++)h=(h^unsigned(reinterpret_cast<uintptr_t>(obj->models[i])))*16777619u;
    return h;
}
static int findBone(const native::DObj* obj,const char* name){
    int offset=0;
    for(int m=0;m<obj->numModels;m++){auto* model=obj->models[m];for(int i=0;i<model->numBones;i++)if(alias(name,native::string(model->boneNames[i])))return offset+i;offset+=model->numBones;}
    return -1;
}
static bool buildBinding(Actor& a,const native::DObj* obj){
    if(!obj->models||!obj->numModels||!obj->numBones||obj->numBones>160)return false;
    a.count=obj->numBones;std::fill_n(a.parent,160,short(-1));std::fill_n(a.body,160,short(-1));std::fill_n(a.duplicate,160,short(-1));
    // Resolve shared model bones before evaluating their descendants. A facial
    // model's spine/neck/head alias the body's bones; calculating the face in
    // its own origin first gives every facial attachment a false translation.
    if(obj->duplicates){
        const auto* data=reinterpret_cast<const uint8_t*>(native::string(obj->duplicates));
        if(!rig::aliases({data,obj->duplicateBytes},{a.duplicate,size_t(a.count)}))return false;
    }
    const auto* attachment=reinterpret_cast<const uint8_t*>(obj->models+obj->numModels);
    std::vector<rig::ModelHierarchy> layout;layout.reserve(obj->numModels);
    for(unsigned m=0;m<obj->numModels;++m){
        const auto* model=obj->models[m];if(!model)return false;
        if(model->numBones>model->numRootBones&&(!model->quats||!model->trans))return false;
        layout.push_back({model->numBones,model->numRootBones,model->parents,attachment[m]});
    }
    if(!rig::hierarchy(layout,{a.duplicate,size_t(a.count)},{a.parent,size_t(a.count)}))return false;
    Vec3 base[160]{};Quat baseQ[160]{};
    int offset=0;
    for(int m=0;m<obj->numModels;m++){
        auto* model=obj->models[m];
        for(int j=0;j<model->numBones;j++){
            int i=offset+j;bool root=j<model->numRootBones;
            Quat local=qIdentity();Vec3 localP{};
            if(!root){auto* source=model->quats+4*(j-model->numRootBones);local=qnormalize(q4(source[0]/32767.f,source[1]/32767.f,source[2]/32767.f,source[3]/32767.f));localP=*reinterpret_cast<Vec3*>(model->trans+3*(j-model->numRootBones));}
            if(a.duplicate[i]>=0){local=qIdentity();localP={};}
            int p=a.parent[i];if(p>=i)return false;
            baseQ[i]=p>=0?qnormalize(baseQ[p]*local):local;base[i]=p>=0?base[p]+rotate(baseQ[p],localP):localP;
        }
        offset+=model->numBones;
    }
    static const int fallback[16]={-1,0,1,2,0,4,5,0,7,8,8,10,11,8,13,14};
    for(int i=0;i<16;i++){
        int bone=findBone(obj,bones[i].name);a.valid[i]=bone>=0;
        if(bone<0){if(fallback[i]<0)return false;bone=a.bone[fallback[i]];}else a.body[bone]=short(i);
        a.bone[i]=static_cast<unsigned char>(bone);int tip=findBone(obj,bones[i].tip);
        Vec3 along=tip>=0?base[tip]-base[bone]:rotate(baseQ[bone],v3(1,0,0));
        if(lengthSq(along)<1e-4f)along=rotate(baseQ[bone],v3(1,0,0));
        a.offset[i]=qnormalize(conj(baseQ[bone])*(i==3||i==6?qIdentity():anatomical(along,bones[i].arm)));
    }
    for(int i=0;i<a.count;i++){int p=a.parent[i];a.attachment[i]=p>=0?rotateInv(baseQ[p],base[i]-base[p]):v3(0,0,0);}
    if(testEnabled){
        float maxError=0;int aliasCount=0,offset=0;
        for(int m=0;m<obj->numModels;m++){
            auto* x=obj->models[m];
            for(int j=x->numRootBones;j<x->numBones;j++){
                int i=offset+j;if(a.duplicate[i]>=0)continue;
                Vec3 expected=*reinterpret_cast<const Vec3*>(x->trans+3*(j-x->numRootBones));
                maxError=std::max(maxError,length(a.attachment[i]-expected));
            }
            offset+=x->numBones;
        }
        for(int i=0;i<a.count;i++)if(a.duplicate[i]>=0)aliasCount++;
        log("STEAM SKIN AUDIT rig %u aliases %d attachment_error %.6f",obj->entnum-1,aliasCount,maxError);
    }
    a.skin=skin(obj);a.bound=true;a.sampleTime=0;a.sampleStamp=0;a.capturedStamp=-1;
    log("Steam rig bound: entity %u object %p client %d model %s bones %d models %u pelvis %u",obj->entnum-1,obj,native::isClientObject(obj),obj->models[0]->name,a.count,obj->numModels,a.bone[0]);return true;
}
static bool bind(Actor& a,const native::DObj* obj){
    // A rejected attachment/model must not leave a partly replaced live rig.
    Actor candidate=a;
    if(!buildBinding(candidate,obj))return false;
    a=std::move(candidate);return true;
}
static bool loadApi(const std::filesystem::path& root){
    HMODULE dll=LoadLibraryExW((root/L"runtime/nm108_native_bridge.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!dll){log("runtime DLL load failed %lu",GetLastError());return false;}
#define GET(name) g_api.name=reinterpret_cast<decltype(g_api.name)>(GetProcAddress(dll,"rd_" #name));if(!g_api.name){log("missing runtime rd_" #name);return false;}
    GET(world_create_ex);GET(world_destroy);GET(load_art);GET(model_rest_bodies);GET(model_rest_segments);
    GET(add_static_mesh);GET(add_static_convex);GET(actor_spawn);GET(actor_despawn);GET(actor_set_mode);
    GET(actor_set_pose_rotations);GET(actor_set_bodies);GET(actor_set_motion);GET(actor_get_bodies);
    GET(actor_get_joint_positions);GET(actor_get_state);GET(world_update);GET(actor_hit);
    GET(actor_get_render_bodies);GET(actor_get_body_velocities);GET(actor_hit_profile);GET(weapon_response);
    GET(world_set_async);GET(actor_get_bounds);GET(actor_get_box_vertices);GET(actor_set_detached_parts);GET(actor_can_activate);
    GET(actor_push);GET(model_body_masses);GET(actor_set_gravity);
#undef GET
    return true;
}
static void clearSupport(Actor& a){a.balancedSince=0;a.supportSamples=0;}
static void release(Actor& a){if(a.active&&g_world)g_api.actor_despawn(g_world,a.handle);a.active=false;a.returning=false;a.readyRelease=false;clearSupport(a);a.blend.reset();}
static void reset(){for(auto& a:actors)a={};if(g_world)g_api.world_destroy(g_world);g_world=nullptr;map=nullptr;lastStep=0;}
static bool uploadMap(native::ClipMap& cm);
static bool world(native::ClipMap* cm){
    if(!modSelected||!cm||!cm->name||strncmp(cm->name,"maps/zombie_",12)){
        if(g_world)reset();
        return false;
    }
    if(mapFailed&&mapName==cm->name&&native::time()>=lastLevelTime){lastLevelTime=native::time();return false;}
    if(g_world&&mapName==cm->name&&native::time()>=lastLevelTime){lastLevelTime=native::time();return true;}
    reset();lastLevelTime=native::time();map=cm;mapName=cm->name;mapFailed=true;
    wchar_t directory[MAX_PATH];GetEnvironmentVariableW(L"BO1_STEAM_DELIRIUM_ROOT",directory,MAX_PATH);
    if(!g_api.world_create_ex&&!loadApi(directory))return false;
    RdWorldDesc description{39.37007874f,1};g_world=g_api.world_create_ex(&description);
    if(!g_world){log("runtime world creation failed");return false;}
    char art[32768]{};GetEnvironmentVariableA("BO1_DELIRIUM_CHARACTER",art,sizeof(art));
    if(g_api.load_art(g_world,art,&model)||g_api.model_rest_bodies(g_world,model,rest)){log("runtime model load failed");reset();return false;}
    RdVec3 starts[16],ends[16];if(g_api.model_rest_segments(g_world,model,starts,ends)){reset();return false;}
    for(int i=0;i<16;i++)bodyOffset[i]=qnormalize(conj(q(rest[i].rotation))*(i==3||i==6?qIdentity():anatomical(v(ends[i])-v(starts[i]),bones[i].arm)));
    if(!uploadMap(*cm)){log("Steam world collision import refused");reset();return false;}
    g_api.world_set_async(g_world,1);lastStep=millis();lastRenderStamp=-1;mapFailed=false;log("Steam physical world ready: %s",cm->name);return true;
}
static unsigned profile(int weapon){
    if(weapon<=0||unsigned(weapon)>*reinterpret_cast<unsigned*>(0xbe1fb0))return 1;
    void* def=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x425770)(weapon);
    void* variant=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x444740)(weapon);
    if(!def)return 1;int kind=native::at<int>(def,0x20);const char* name=variant?native::at<const char*>(variant,0):"";
    if(kind==2)return 3;if(kind==0||kind==1||kind==7){if(strstr(name,"l96")||strstr(name,"dragunov")||strstr(name,"psg1")||strstr(name,"wa2000"))return 5;return 4;}
    if(kind==3)return strstr(name,"ithaca")||strstr(name,"olympia")?6:7;
    if(kind==4&&(strstr(name,"python")||strstr(name,"deagle")))return 2;return 1;
}
static bool activate(Actor& a,void* ent,int now){
    if(!g_world||!a.bound||a.active||now-a.sampleTime>250)return a.active;
    // Let native scripted traversals own their root until code movement resumes.
    if(!a.dead){auto* owner=native::at<void*>(ent,0x140);if(!owner||native::at<int>(owner,0xde0)!=1)return false;}
    bool incoming=now>=a.incomingTime&&now-a.incomingTime<500;
    Vec3 velocity=incoming?a.incoming:a.velocity;
    RdQuat rotations[16];for(int i=0;i<16;i++)rotations[i]=r(qnormalize(a.animationQ[a.bone[i]]*a.offset[i]*conj(bodyOffset[i])));
    RdTransform pelvis{r(a.animation[a.bone[0]]),rotations[0]};unsigned handle;
    RdTransform root{pelvis.position,{0,0,0,1}};
    if(g_api.actor_spawn(g_world,model,&root,r(velocity),&handle))return false;
    a.handle=handle;
    if(g_api.actor_set_gravity(g_world,handle,a.gravityScale)){g_api.actor_despawn(g_world,handle);return false;}
    if(g_api.actor_set_pose_rotations(g_world,handle,&pelvis,rotations,r(velocity))){g_api.actor_despawn(g_world,handle);return false;}
    RdVec3 joints[16];if(!g_api.actor_get_joint_positions(g_world,handle,joints)){
        Vec3 delta=(a.animation[a.bone[3]]+a.animation[a.bone[6]])*.5f-(v(joints[3])+v(joints[6]))*.5f;
        if(lengthSq(delta)<1600.f){pelvis.position=r(v(pelvis.position)+delta);g_api.actor_set_pose_rotations(g_world,handle,&pelvis,rotations,r(velocity));}}
    float lift=0;
    RdVec3 lower[16],upper[16];
    if(!g_api.actor_get_bounds(g_world,handle,lower,upper)){
        for(int foot:{3,6}){
            if(!present(a,foot))continue;RdVec3 corners[8];if(g_api.actor_get_box_vertices(g_world,handle,foot,corners))continue;
            for(auto corner:corners){
                float start[3]={corner.x,corner.y,upper[foot].z+.25f},end[3]={corner.x,corner.y,corner.z-32.f},zero[3]{};
                auto trace=native::trace(start,end,zero,zero,native::at<int>(ent,0));
                if(!trace.startSolid&&!trace.allSolid&&trace.fraction<1.f&&trace.normal[2]>.55f){
                    float amount=start[2]+(end[2]-start[2])*trace.fraction+.25f-corner.z;
                    if(amount>lift&&amount<16.f)lift=amount;
                }
            }
        }
        if(lift>0){pelvis.position.z+=lift;g_api.actor_set_pose_rotations(g_world,handle,&pelvis,rotations,r(velocity));}
        if(g_api.actor_get_bounds(g_world,handle,lower,upper)==0){
            for(int part:{0,7,8,9}){
                Vec3 center=(v(lower[part])+v(upper[part]))*.5f,extent=(v(upper[part])-v(lower[part]))*.4f,lo=-extent;
                auto trace=native::trace(&center.x,&center.x,&lo.x,&extent.x,native::at<int>(ent,0));
                if(trace.startSolid||trace.allSolid){g_api.actor_despawn(g_world,handle);return false;}
            }
        }
    }
    if(g_api.actor_can_activate(g_world,handle)==0){g_api.actor_despawn(g_world,handle);return false;}
    RdTransform body[16];if(g_api.actor_get_bodies(g_world,handle,body,16)){g_api.actor_despawn(g_world,handle);return false;}
    RdVec3 linear[16],angular[16];for(int i=0;i<16;i++){
        Vec3 offset=v(body[i].position)-a.animation[a.bone[i]];
        Vec3 spin=incoming?a.incomingAngular[i]:a.angular[i];
        linear[i]=r((incoming?a.incomingLinear[i]:a.linear[i])+cross(spin,offset));angular[i]=r(spin);
    }
    if(g_api.actor_set_bodies(g_world,handle,body,linear,angular)){g_api.actor_despawn(g_world,handle);return false;}
    a.pelvisOffset=rotateInv(q(body[0].rotation),a.animation[a.bone[0]]+v3(0,0,lift)-v(body[0].position));
    unsigned detached=a.detachedMask;for(int i=0;i<16;i++)if(!a.valid[i])detached|=1u<<i;
    a.detachedMask=detached;
    if(detached)g_api.actor_set_detached_parts(g_world,handle,detached);
    if(g_api.actor_set_mode(g_world,handle,a.dead?0:1)){g_api.actor_despawn(g_world,handle);return false;}
    a.active=true;a.frozen=false;a.pendingContact=false;a.activeSince=now;clearSupport(a);a.blend.reset();a.run.cancel();
    std::copy_n(a.animation,a.count,a.draw);std::copy_n(a.animationQ,a.count,a.drawQ);
    log("Steam physics takeover entity %d handle %u incoming %.2f %.2f %.2f pelvis %.2f %.2f %.2f",native::at<int>(ent,0),handle,velocity.x,velocity.y,velocity.z,linear[0].x,linear[0].y,linear[0].z);return true;
}
static void frame(int renderStamp){
    if(!g_world)return;uint64_t now=millis();unsigned elapsed=unsigned(now-lastStep);
    if(renderStamp<0){if(elapsed<50)return;}else{if(lastRenderStamp==renderStamp)return;lastRenderStamp=renderStamp;}
    if(!elapsed)return;lastStep=now;
    if(elapsed>500)return;
    if(g_api.world_update(g_world,float(elapsed)*.001f)<0){log("Steam physical runtime failure; releasing ownership");reset();return;}
    for(unsigned ent=0;ent<actors.size();ent++){
        auto& a=actors[ent];if(!a.active)continue;
        int type=native::at<uint16_t>(native::entity(ent),0xbe);
        if(type!=16&&type!=18){release(a);a={};continue;}
        if(g_api.actor_get_state(g_world,a.handle,&a.status))continue;
        int t=native::time();float dt=float(elapsed)*.001f;
        if(a.dead){
            a.blend.step(false,dt);
            // Same quiet-body policy as the working host: retain the completed
            // visible pose, but free a solver slot once a corpse has settled.
            RdVec3 linear[16],angular[16];bool quiet=t-a.deadSince>2500;
            if(g_api.actor_get_body_velocities(g_world,a.handle,linear,angular))quiet=false;
            else for(int i=0;i<16;i++)if(present(a,i)&&(lengthSq(v(linear[i]))>7.874f*7.874f||lengthSq(v(angular[i]))>.35f*.35f))quiet=false;
            if(!quiet)a.settledSince=0;
            else if(!a.settledSince)a.settledSince=t;
            else if(t-a.settledSince>750){release(a);a.frozen=true;log("Steam corpse sleeping entity %u",ent);}
            continue;
        }
        const bool interrupted=animationReturnInterrupted(int(a.status.flags),false,a.hits.ready(t));
        void* owner=native::at<void*>(native::entity(ent),0x140);
        if(interrupted||!owner){a.returning=false;a.readyRelease=false;clearSupport(a);a.run.cancel();}
        if(!a.returning&&(a.status.flags&RD_STATE_BALANCED)&&!interrupted&&owner&&t-a.activeSince>900){
            if(!a.balancedSince)a.balancedSince=t;
            if(!a.supportSamples||a.supportTime!=t){
                a.supportedVelocity[a.supportSamples++%4]=v(a.status.com_velocity);
                a.supportTime=t;
            }
            if(t-a.balancedSince>=200){
                a.returning=true;
                float yaw=native::at<Vec3>(native::entity(ent),0x128).y*DEG2RAD;
                Vec3 carriedVelocity{};
                unsigned count=std::min(a.supportSamples,4u);
                for(unsigned i=0;i<count;i++)carriedVelocity+=a.supportedVelocity[i];
                const float carried=std::max(0.f,dot(carriedVelocity,v3(cosf(yaw),sinf(yaw),0))/float(count));
                a.run.begin(t,.65f,carried,a.nominalSpeed);
            }
        }else if(!a.returning&&!(a.status.flags&RD_STATE_BALANCED))clearSupport(a);
        float weight=a.blend.step(a.returning,dt);
        if(a.returning&&weight<=.001f){
            // The server must commit the final physical position after its
            // scripts, before relinquishing ownership and sending a snapshot.
            a.readyRelease=true;
        }
    }
}
static void hit(void* ent,int damage,const float* point,int mod,const float* direction,int location,int weapon,bool dead,const float* blastOrigin=nullptr){
    std::lock_guard guard(mutex);int index=native::at<int>(ent,0);if(index<0||index>=1024)return;
    auto& a=actors[index];if(!a.bound||!g_world)return;
    int now=native::time();if(dead&&!a.dead)a.deadSince=now;a.dead|=dead;a.lastImpact=now;
    if(a.frozen){
        std::copy_n(a.draw,a.count,a.animation);std::copy_n(a.drawQ,a.count,a.animationQ);
        a.sampleTime=now;a.incomingTime=0;a.rootVelocity={};a.velocity={};
        std::fill_n(a.linear,16,Vec3{});std::fill_n(a.angular,16,Vec3{});
    }
    const auto route=bo1damage::route(mod);
    const bool explosion=route==bo1damage::Route::Explosion,melee=route==bo1damage::Route::Melee;
    // Native fall, burn, gas and trigger damage have no projectile momentum.
    // Their lethal response still inherits the body's current motion below.
    if(route==bo1damage::Route::Environment&&!dead)return;
    bool fullWound=dead||explosion||melee||a.wounds.hit(damage,native::at<int>(ent,0x184)+damage,now);
    static const int parts[]={8,9,9,9,8,7,13,10,14,11,15,12,4,1,5,2,6,3,8};
    int body=location>=0&&location<19?parts[location]:8;
    Vec3 p=point?Vec3{point[0],point[1],point[2]}:a.animation[a.bone[body]];
    Vec3 dir=direction?normalize(Vec3{direction[0],direction[1],direction[2]}):v3(0,0,1);
    if(!finite3(p)||!finite3(dir))return;
    unsigned kind=profile(weapon);float impulse=0,duration=0,speed=0;
    if(explosion){
        // Preserve the proven BO1 blast adaptation, distributed by ART mass below.
        Vec3 away=blastOrigin?origin(ent)+v3(0,0,35)-Vec3{blastOrigin[0],blastOrigin[1],blastOrigin[2]}:dir;
        away.z=std::max(length(v3(away.x,away.y,0))*.55f,24.f);
        a.blast+=normalize(away)*clampf(3.f+damage*.016f,3.f,10.f);
        float n=length(a.blast);if(n>12)a.blast=a.blast*(12.f/n);
    }else if(route==bo1damage::Route::Bullet||melee){
        if(melee){kind=10;impulse=75.f*1.5f*.15f;duration=.15f;speed=450;}
        else if(g_api.weapon_response(kind,&impulse,&duration,&speed)){log("missing verified weapon response %u",kind);return;}
        if(!fullWound&&!a.active&&!a.hits.ready(now)){
            if(kind==6||kind==7){
                if(a.weakPellets.count&&a.weakPellets.hits[0].time!=now)a.weakPellets.count=0;
                a.weakPellets.push(p,dir*impulse,body,now,duration,speed,8);
            }
            return;
        }
        if(!fullWound)kind=8;
        a.hits.push(p,dir*impulse,body,now,duration,speed,kind);
        for(int i=0;i<a.weakPellets.count;i++){auto& h=a.weakPellets.hits[i];if(h.time==now)a.hits.push(h.point,h.impulse,h.body,now,h.duration,h.bulletSpeed,8);}
        a.weakPellets.count=0;
    }
    if(!a.active&&(!a.incomingTime||now-a.incomingTime>=500)){
        void* owner=native::at<void*>(ent,0x140);a.incoming=owner?native::at<Vec3>(owner,0xdf8):a.velocity;
        if(!finite3(a.incoming))a.incoming=a.velocity;
        a.incomingTime=now;
        for(int i=0;i<16;i++){a.incomingLinear[i]=a.linear[i]-a.rootVelocity+a.incoming;a.incomingAngular[i]=a.angular[i];}
    }
    if(!a.active)activate(a,ent,now);
    if(a.active){a.returning=false;a.readyRelease=false;a.run.cancel();clearSupport(a);if(a.dead)g_api.actor_set_mode(g_world,a.handle,0);}
    if(testEnabled)log("Steam physical impact entity %d damage %d mod %d body %d profile %u impulse %.3f active %d dead %d",index,damage,mod,body,kind,impulse,a.active,a.dead);
}
static void flushHits(Actor& a,int now){
    if(!a.active)return;a.hits.expire(now);
    for(int i=0;i<a.hits.count;i++){auto& h=a.hits.hits[i];RdHit hit{unsigned(h.body),r(h.point),r(h.impulse)};
        int result=h.profile==8?g_api.actor_push(g_world,a.handle,h.body,r(h.impulse),h.duration,8):g_api.actor_hit_profile(g_world,a.handle,&hit,h.duration,h.bulletSpeed,h.profile);
        if(result)log("Steam impact refused handle %u kind %u result %d",a.handle,h.profile,result);
    }
    a.hits.count=0;
    if(lengthSq(a.blast)>0){float masses[16];if(!g_api.model_body_masses(g_world,masses)){
        for(int i=0;i<16;i++)if(present(a,i)){Vec3 j=a.blast*masses[i];float n=length(j);if(n>127)j=j*(127.f/n);g_api.actor_push(g_world,a.handle,i,r(j),.08f,9);}
        log("Steam blast applied handle %u deltaV %.2f %.2f %.2f",a.handle,a.blast.x,a.blast.y,a.blast.z);a.blast={};
    }}
}
static void pose(native::DObj* obj){
    std::lock_guard guard(mutex);
    if(!g_world||!obj->skel.mat||!obj->entnum||obj->entnum>1024)return;
    int index=obj->entnum-1;void* ent=native::entity(index);auto& a=actors[index];
    bool client=native::isClientObject(obj);
    if(!native::humanoidActor(ent)&&!((a.active||a.frozen)&&a.dead))return;
    if(client&&!a.frozen){
        if(!a.bound||a.skin!=skin(obj))if(!bind(a,obj))return;
        // Shadows, attachments and hit tests can request the same completed
        // palette again. Never feed our own written physics pose back into
        // animation history or seed velocity from another pass of this frame.
        if(a.capturedStamp!=obj->skel.timestamp){
        frame(obj->skel.timestamp);if(!g_world)return;
        int now=native::time();
        for(int i=0;i<a.count;i++){a.animation[i]=pos(obj->skel.mat[i]);a.animationQ[i]=rot(obj->skel.mat[i]);if(!finite3(a.animation[i])||!finiteq(a.animationQ[i]))return;}
        // Only a completed predecessor from this same actor/rig can seed momentum.
        uint64_t stamp=millis();
        if(a.sampleStamp&&stamp>a.sampleStamp&&stamp-a.sampleStamp<=150){
            float inv=1000.f/float(stamp-a.sampleStamp);a.rootVelocity=(a.animation[0]-a.previous[0])*inv;a.velocity=a.rootVelocity;
            for(int i=0;i<16;i++){
                int bone=a.bone[i];a.linear[i]=(a.animation[bone]-a.previous[bone])*inv;
                Quat delta=qnormalize(a.animationQ[bone]*conj(a.previousQ[bone]));if(delta.w<0)delta=q4(-delta.x,-delta.y,-delta.z,-delta.w);
                Vec3 axis=v3(delta.x,delta.y,delta.z);float n=length(axis);
                a.angular[i]=n>1e-6f?axis*(2.f*atan2f(n,delta.w)*inv/n):v3(0,0,0);
            }
        }
        if(stamp!=a.sampleStamp){std::copy_n(a.animation,a.count,a.previous);std::copy_n(a.animationQ,a.count,a.previousQ);a.sampleTime=now;a.sampleStamp=stamp;a.previousOrigin=origin(ent);}
        a.capturedStamp=obj->skel.timestamp;
        if((a.hits.ready(now)||lengthSq(a.blast)>0||(a.pendingContact&&now-a.contactTime<500))&&!a.active)activate(a,ent,now);
        flushHits(a,now);
        if(!a.active)return;
        RdTransform body[16];if(g_api.actor_get_render_bodies(g_world,a.handle,body,16))return;
        Quat physical[16];for(int i=0;i<16;i++)physical[i]=qnormalize(q(body[i].rotation)*bodyOffset[i]*conj(a.offset[i]));
        Vec3 pelvis=v(body[0].position)+rotate(q(body[0].rotation),a.pelvisOffset);
        retargetHierarchy(a.count,a.parent,a.body,a.bone[0],a.animation,a.animationQ,physical,pelvis,a.blend.weight,a.draw,a.drawQ,a.attachment,a.duplicate);
        if(testEnabled){
            static int audited=0;
            if(audited<20){float drift=0;int aliases=0;
                for(int i=0;i<a.count;i++)if(a.duplicate[i]>=0){aliases++;drift=std::max(drift,length(a.draw[i]-a.draw[a.duplicate[i]]));}
                log("STEAM SKIN AUDIT physical %d aliases %d duplicate_drift %.6f",index,aliases,drift);audited++;
            }
        }
        }
    }
    if((!a.active&&!a.frozen)||!a.bound||a.count!=obj->numBones)return;
    float yaw=native::at<Vec3>(ent,0x128).y*DEG2RAD;Quat owner=qFromAxisAngle(v3(0,0,1),yaw);
    for(int i=0;i<a.count;i++){
        Vec3 p=client?a.draw[i]:rotateInv(owner,a.draw[i]-origin(ent));Quat orientation=client?a.drawQ[i]:qnormalize(conj(owner)*a.drawQ[i]);
        if(!finite3(p)||!finiteq(orientation))return;
        auto& mat=obj->skel.mat[i];memcpy(mat.quat,&orientation,16);memcpy(mat.trans,&p,12);mat.transWeight=2.f;
        obj->skel.skel[i>>5]|=0x80000000u>>(i&31);
    }
}
static void corpseRoot(void* ent){
    std::lock_guard guard(mutex);if(!g_world)return;
    int index=native::at<int>(ent,0);if(index<0||index>=1024)return;
    auto& a=actors[index];if(!a.dead||(!a.active&&!a.frozen))return;
    int type=native::at<uint16_t>(ent,0xbe);if(type!=16&&type!=18)return;
    frame(-1);if(!g_world)return;
    Vec3 root=a.draw[a.bone[0]];
    if(a.active){RdTransform bodies[16];if(g_api.actor_get_bodies(g_world,a.handle,bodies,16))return;root=v(bodies[0].position);}
    if(!finite3(root))return;
    native::at<Vec3>(ent,0x11c)=root;native::at<Vec3>(ent,0x18)=root;
    reinterpret_cast<void(__cdecl*)(void*)>(0x695450)(ent);
}
static void freed(void* ent){
    std::lock_guard guard(mutex);int index=native::at<int>(ent,0);if(index<0||index>=1024)return;
    auto& a=actors[index];if(a.bound)log("Steam actor released entity %d active %d frozen %d",index,a.active,a.frozen);
    release(a);a={};
}
static void dismember(int index,unsigned parameter){
    std::lock_guard guard(mutex);if(!g_world||index<0||index>=1024)return;
    auto& a=actors[index];unsigned tags=parameter&255,old=a.detachedMask;bool all=(tags&1)!=0;
    if(old&~(1u<<9)){tags&=1u<<5;all=false;}
    if(tags&(1u<<6))a.detachedMask|=(1u<<10)|(1u<<11)|(1u<<12);
    constexpr unsigned masks[5]={(1u<<14)|(1u<<15),(1u<<11)|(1u<<12),(1u<<5)|(1u<<6),(1u<<2)|(1u<<3),1u<<9};
    for(int piece=0;piece<5;piece++)if(all||(tags&(1u<<(piece+1))))a.detachedMask|=masks[piece];
    if(old!=a.detachedMask){
        if(a.active)g_api.actor_set_detached_parts(g_world,a.handle,a.detachedMask);
        a.returning=false;a.readyRelease=false;clearSupport(a);
        log("Steam native dismember entity %d mask %04X",index,a.detachedMask);
    }
}
static void crowd(void* ent,Actor& source,Vec3 before,Vec3 velocity){
    const int now=native::time();
    if(!source.bound||source.dead||source.active||now-source.sampleTime>150)return;
    void* owner=native::at<void*>(ent,0x140);if(!owner)return;
    Vec3 mins=native::at<Vec3>(owner,0xe38),maxs=native::at<Vec3>(owner,0xe44);
    float radius=std::max({fabsf(mins.x),fabsf(mins.y),fabsf(maxs.x),fabsf(maxs.y)});
    if(!finite3(mins)||!finite3(maxs)||radius<=0||radius>100||maxs.z<=mins.z)return;
    constexpr float dt=.05f;
    static const unsigned tips[16]={7,2,3,3,5,6,6,8,9,9,11,12,12,14,15,15};
    Vec3 ownLow[16],ownHigh[16],shift=before-source.previousOrigin;
    for(int p=0;p<16;p++){
        Vec3 a=source.animation[source.bone[p]]+shift,b=source.animation[source.bone[tips[p]]]+shift;
        float radiusPart=(p==0||p==7||p==8)?6.f:(p==9?5.f:3.f);
        for(int k=0;k<3;k++){(&ownLow[p].x)[k]=std::min((&a.x)[k],(&b.x)[k])-radiusPart;(&ownHigh[p].x)[k]=std::max((&a.x)[k],(&b.x)[k])+radiusPart;}
    }
    for(unsigned index=0;index<actors.size();index++){
        auto& target=actors[index];if(&target==&source||!target.active)continue;
        if(lengthSq(origin(ent)-origin(native::entity(index)))>200.f*200.f)continue;
        RdVec3 low[16],high[16];if(g_api.actor_get_bounds(g_world,target.handle,low,high))continue;
        if(!target.dead){
            CrowdMotion motion{};RdVec3 linear[16],angular[16];RdTransform transforms[16];
            if(!g_api.model_body_masses(g_world,motion.mass)&&!g_api.actor_get_body_velocities(g_world,target.handle,linear,angular)&&!g_api.actor_get_bodies(g_world,target.handle,transforms,16)){
                for(int p=0;p<16;p++)if(present(target,p)){motion.totalMass+=motion.mass[p];motion.center[p]=v(transforms[p].position);motion.linear[p]=v(linear[p]);motion.angular[p]=v(angular[p]);}
                for(int p=0;p<16;p++)if(present(target,p))for(int own=0;own<16;own++)if(present(source,own)){
                    Vec3 point=(v(low[p])+v(high[p]))*.5f;
                    Vec3 moving=crowdPointVelocity(motion,p,point);CrowdHit contact{};
                    if(!crowdPartSweep(ownLow[own],ownHigh[own],velocity*dt-moving*dt,v(low[p]),v(high[p]),contact))continue;
                    moving=crowdPointVelocity(motion,p,contact.point);
                    float momentum=crowdPartMomentum(v(target.status.com_velocity),moving,velocity,contact.normal,motion.totalMass,motion.mass[p],motion.totalMass);
                    if(!crowdReactiveImpact(true,false,momentum))continue;
                    Vec3 from={before.x,before.y,contact.point.z},zero{};
                    auto wall=native::trace(&from.x,&contact.point.x,&zero.x,&zero.x,native::at<int>(ent,0));
                    if(wall.startSolid||wall.allSolid||wall.fraction<.999f)continue;
                    source.pendingContact=true;source.contactTime=source.incomingTime=now;source.incoming=velocity;
                    for(int i=0;i<16;i++){source.incomingLinear[i]=source.linear[i]-source.rootVelocity+velocity;source.incomingAngular[i]=source.angular[i];}
                    log("Steam incoming physical contact receiver %d source %u part %d budget %.2f Ns",native::at<int>(ent,0),index,p,momentum);
                    activate(source,ent,now);return;
                }
            }
        }
        // The working host's prescribed-driver contact: animated pursuit is
        // never stopped by a settled body or another ordinary animated actor.
        unsigned part=16;float best=radius*radius;Vec3 point{};
        Vec3 current=origin(ent);
        for(unsigned p=0;p<16;p++)if(present(target,p)){
            if(high[p].z<current.z+mins.z||low[p].z>current.z+maxs.z)continue;
            Vec3 closest={clampf(current.x,low[p].x,high[p].x),clampf(current.y,low[p].y,high[p].y),clampf(current.z+35.f,low[p].z,high[p].z)};
            float distance=(closest.x-current.x)*(closest.x-current.x)+(closest.y-current.y)*(closest.y-current.y);
            if(distance<best){best=distance;part=p;point=closest;}
        }
        if(part==16)continue;Vec3 normal=point-current;normal.z=0;if(lengthSq(normal)<.1f)normal=velocity;normal=normalize(normal);
        float closing=dot(velocity-v(target.status.com_velocity),normal);if(closing<=5.f)continue;
        Vec3 zero{},from={current.x,current.y,point.z};auto wall=native::trace(&from.x,&point.x,&zero.x,&zero.x,native::at<int>(ent,0));
        if(wall.startSolid||wall.allSolid||wall.fraction<.999f)continue;
        if(target.pushTime!=now){target.pushTime=now;target.pushBudget=240.f*dt;}
        float amount=std::min({clampf(closing/39.37f*60.f,0.f,240.f)*dt,12.f,target.pushBudget});
        if(amount<=0)continue;target.pushBudget-=amount;
        g_api.actor_push(g_world,target.handle,part,r(normal*amount),.05f,8);
    }
}
static void afterThink(void* ent,Vec3 before){
    std::lock_guard guard(mutex);if(!g_world)return;
    frame(-1);if(!g_world)return;
    int index=native::at<int>(ent,0);if(index<0||index>=1024||!native::actor(ent))return;
    auto& a=actors[index];void* owner=native::at<void*>(ent,0x140);
    if(a.owner&&a.owner!=owner){release(a);a={};}a.owner=owner;
    const float gravity=native::actorGravityScale(ent);
    if(std::isfinite(gravity)&&gravity>=0.f&&gravity<=1.f&&gravity!=a.gravityScale){
        if(!a.active||!g_api.actor_set_gravity(g_world,a.handle,gravity)){
            a.gravityScale=gravity;
        }
    }
    Vec3 intended=(origin(ent)-before)*20.f;
    if(!a.active)crowd(ent,a,before,intended);
    if(!a.active){float speed=length(v3(intended.x,intended.y,0));if(speed>1.f&&!a.run.active)a.nominalSpeed=speed;return;}
    if(a.dead)return;
    a.desired=intended;float yaw=native::at<Vec3>(ent,0x128).y*DEG2RAD;
    g_api.actor_set_motion(g_world,a.handle,r(intended),{cosf(yaw),sinf(yaw),0});
    RdTransform bodies[16];if(g_api.actor_get_bodies(g_world,a.handle,bodies,16))return;
    Vec3 root=v(bodies[0].position);if(!finite3(root))return;
    Vec3 end=root-v3(0,0,96);float lo[3]={-4,-4,0},hi[3]={4,4,8};
    auto floor=native::trace(&root.x,&end.x,lo,hi,index);if(floor.startSolid||floor.allSolid)return;
    if(floor.fraction<1.f)root.z+=(end.z-root.z)*floor.fraction;
    native::at<Vec3>(ent,0x11c)=root;native::at<Vec3>(ent,0x18)=root;
    native::at<Vec3>(owner,0xdec)=root;native::at<Vec3>(owner,0xdf8)={};
    // sentient +0x14 is vOldOrigin, not another authoritative transform.
    // Actor_Think compares it with the displaced origin to invalidate its
    // navigation/perception caches. Let the original engine update it.
    reinterpret_cast<void(__cdecl*)(void*)>(0x695450)(ent);
}
static void postFrame(){
    std::lock_guard guard(mutex);if(!g_world)return;
    for(unsigned index=0;index<actors.size();index++){
        auto& a=actors[index];if(!a.active&&!a.frozen)continue;
        void* ent=native::entity(index);
        if(a.dead){corpseRoot(ent);continue;}
        if(!a.active||!native::actor(ent))continue;
        RdTransform bodies[16];if(g_api.actor_get_bodies(g_world,a.handle,bodies,16))continue;
        Vec3 root=v(bodies[0].position),end=root-v3(0,0,96);if(!finite3(root))continue;
        float low[3]={-4,-4,0},high[3]={4,4,8};auto floor=native::trace(&root.x,&end.x,low,high,index);
        if(floor.startSolid||floor.allSolid)continue;
        if(floor.fraction<1.f)root.z+=(end.z-root.z)*floor.fraction;
        native::at<Vec3>(ent,0x11c)=root;native::at<Vec3>(ent,0x18)=root;
        void* owner=native::at<void*>(ent,0x140);native::at<Vec3>(owner,0xdec)=root;native::at<Vec3>(owner,0xdf8)={};
        reinterpret_cast<void(__cdecl*)(void*)>(0x695450)(ent);
        if(a.readyRelease){
            a.readyRelease=false;
            log("Steam physics recovered entity %u nominal %.2f runScale %.3f origin %.2f %.2f %.2f",index,a.nominalSpeed,a.runScale,root.x,root.y,root.z);
            release(a);
        }
    }
}
static void animationRate(native::DObj* obj,Actor& a,bool client,float scale){
    uint16_t info=0;float* value=native::rootRate(obj,info);if(!value||!std::isfinite(*value))return;
    auto& rate=a.rate[client?1:0];
    if(rate.tree!=obj->tree||rate.info!=info){rate={};rate.tree=obj->tree;rate.info=info;}
    if(rate.owned&&fabsf(*value-rate.applied)>.0001f)rate.owned=false;
    if(scale<.9999f){if(!rate.owned)rate.base=*value;rate.applied=rate.base*scale;*value=rate.applied;rate.owned=true;}
    else if(rate.owned){*value=rate.base;rate.owned=false;}
}
static void updateRun(void* ent){
    std::lock_guard guard(mutex);int index=native::at<int>(ent,0);if(index<0||index>=1024)return;
    auto& a=actors[index];if(!a.bound)return;
    void* owner=native::at<void*>(ent,0x140);float scale=1.f;
    if((!a.active||a.returning)&&!a.dead&&owner&&native::at<int>(owner,0xde0)==1&&native::at<int16_t>(owner,0x193e)>0)
        scale=a.run.sample(native::time());
    a.runScale=scale;
    if(auto* obj=native::serverObject(index))animationRate(obj,a,false,scale);
    if(auto* obj=native::clientObject(index))animationRate(obj,a,true,scale);
}
}
