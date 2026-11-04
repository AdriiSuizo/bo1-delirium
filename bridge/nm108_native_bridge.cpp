#include "nm108_hit_batch.h"
// Numeric C ABI adapter for BO1's existing 16-body render bridge. All 21 host
// physical parts, constraints, controllers and collisions stay in the original
// isolated runtime. This adapter supplies host coordinate/bone conversions.
// Original PointArm reach and interruptible original-animation recovery are
// scheduled in the owned runtime; numeric status is exported to the game.
#include <windows.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>
#include <map>
#include <io.h>
#include "nm108_runtime_client.h"
#include "nm108_balance_state.h"
#include "../native/src/core/euphoria_weapon_response.h"
#define RD_API __declspec(dllexport)
#include "../native/src/core/ragdoll.h"
#define API extern "C" __declspec(dllexport)
namespace {
struct V {float x,y,z;};struct Q {float x,y,z,w;};
V operator+(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V operator-(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V operator*(V a,float b){return {a.x*b,a.y*b,a.z*b};}
float Dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V Cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Q Mul(Q a,Q b){return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
Q Inv(Q a){return {-a.x,-a.y,-a.z,a.w};}
Q Unit(Q a){float n=sqrtf(a.x*a.x+a.y*a.y+a.z*a.z+a.w*a.w);return {a.x/n,a.y/n,a.z/n,a.w/n};}
V Rot(Q q,V v){Q p=Mul(Mul(q,{v.x,v.y,v.z,0}),Inv(q));return {p.x,p.y,p.z};}
Q Mix(Q a,Q b,float t){if(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0) b={-b.x,-b.y,-b.z,-b.w};return Unit({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t});}
// Render interpolation clock in ms. GetTickCount64 moves in ~15.6 ms steps,
// so at 60 fps the interpolated bodies advanced in visible jerks.
double ClockMs(){static LARGE_INTEGER f={};if(!f.QuadPart)QueryPerformanceFrequency(&f);LARGE_INTEGER c;QueryPerformanceCounter(&c);return double(c.QuadPart)*1000.0/double(f.QuadPart);}
bool Finite(V v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool Finite(Q q){float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;return std::isfinite(n)&&n>.8f&&n<1.2f;}
Q MatrixQ(const float* m){
    Q q;float t=m[0]+m[5]+m[10];
    if(t>0){float s=sqrtf(t+1)*2;q={(m[6]-m[9])/s,(m[8]-m[2])/s,(m[1]-m[4])/s,s*.25f};}
    else if(m[0]>m[5]&&m[0]>m[10]){float s=sqrtf(1+m[0]-m[5]-m[10])*2;q={s*.25f,(m[4]+m[1])/s,(m[8]+m[2])/s,(m[6]-m[9])/s};}
    else if(m[5]>m[10]){float s=sqrtf(1+m[5]-m[0]-m[10])*2;q={(m[4]+m[1])/s,s*.25f,(m[9]+m[6])/s,(m[8]-m[2])/s};}
    else{float s=sqrtf(1+m[10]-m[0]-m[5])*2;q={(m[8]+m[2])/s,(m[9]+m[6])/s,s*.25f,(m[1]-m[4])/s};}return Unit(q);
}
V Position(const float* m){return {m[12],m[13],m[14]};}
void Matrix(Q q,V p,float* m){
    q=Unit(q);float x=q.x,y=q.y,z=q.z,w=q.w;
    m[0]=1-2*(y*y+z*z);m[1]=2*(x*y+w*z);m[2]=2*(x*z-w*y);m[3]=0;
    m[4]=2*(x*y-w*z);m[5]=1-2*(x*x+z*z);m[6]=2*(y*z+w*x);m[7]=0;
    m[8]=2*(x*z+w*y);m[9]=2*(y*z-w*x);m[10]=1-2*(x*x+y*y);m[11]=0;
    m[12]=p.x;m[13]=p.y;m[14]=p.z;m[15]=0;
}
const Q gtaToHost={0,0,-.7071067811865475f,.7071067811865475f};
V Gta(RdVec3 v,float units){return {-v.y/units,v.x/units,v.z/units};}
RdVec3 Host(V v,float units){return {v.y*units,-v.x*units,v.z*units};}
Q Gta(RdQuat q){return Unit(Mul(Inv(gtaToHost),{q.x,q.y,q.z,q.w}));}
RdQuat Host(Q q){q=Unit(Mul(gtaToHost,q));return {q.x,q.y,q.z,q.w};}
const unsigned parts16[16]={0,1,2,3,4,5,6,7,10,12,14,15,16,18,19,20};
struct BridgeActor {bool allocated=false,created=false,retired=false;uint32_t handle=0,mode=RD_MODE_PASSIVE,detachedMask=0;nm108::InitialPose initial={};nm108::Target target={};nm108::GaitReference gait={};double gaitUntil=0;V forward={0,1,0},shotPressure={};float pain=0,quiet=0,stable=0;bool settling=false,animationReady=false;V settleForward={0,1,0};uint32_t gravity=1000,recoveryAtImpact=0;};
}
namespace BirthClearance {
using GuideV=V;using MotionQ=Q;
static GuideV GuideAdd(GuideV a,GuideV b){return a+b;}
static GuideV GuideSub(GuideV a,GuideV b){return a-b;}
static GuideV GuideRotate(MotionQ q,GuideV v){return Rot(q,v);}
static MotionQ MultiplyQ(MotionQ a,MotionQ b){return Mul(a,b);}
#include "nm108_shape_distance.h"
}
struct RdFragmentDesc {RdTransform transform;RdVec3 dimensions,linear,angular;float mass;};
struct RdWorld {
    std::vector<nm108::Request> lifecycle;
    bool birthPause=false;
    bool fragmentPending[nm108::MaxFragments]={};
    nm108::Client client;float units=39.37f,accumulator=0;bool failed=false,asynchronous=false;unsigned pendingSteps=0,lastBirthFrame=UINT32_MAX;
    std::string workerPath,workerLog;
    unsigned mapRestarts=0;
    FILE* mapCache=nullptr;FILE* initialCache=nullptr;
    ~RdWorld(){if(mapCache)fclose(mapCache);if(initialCache)fclose(initialCache);}
    float rest[21][16];V restAnchor[21]={};nm108::Rig rig;BridgeActor actors[nm108::MaxActors];uint32_t generation=0;
    // Physical data is valid for one complete IPC snapshot, including lifecycle
    // requests at the same solver frame. Pending birth poses are never cached.
    struct PoseCache {uint64_t revision=0;uint32_t handle=0;bool bodiesReady=false,bounds=false,joints=false;
        RdTransform bodies[16];RdVec3 mins[16],maxs[16],anchors[16];};
    mutable PoseCache physical[nm108::MaxActors];
    PoseCache& Cache(unsigned slot) const {
        auto& cache=physical[slot];const auto& actor=actors[slot];
        if(!actor.created||cache.revision!=client.Revision()||cache.handle!=actor.handle){
            cache.revision=actor.created?client.Revision():0;cache.handle=actor.handle;
            cache.bodiesReady=cache.bounds=cache.joints=false;
        }return cache;
    }
    const RdTransform* Bodies(unsigned slot) const {
        auto& cache=Cache(slot);
        if(!cache.bodiesReady){
            for(unsigned p=0;p<16;++p){const auto* pose=Pose(slot,parts16[p]);
                cache.bodies[p]={Host(Position(pose),units),Host(MatrixQ(pose))};}
            cache.bodiesReady=true;
        }return cache.bodies;
    }
    RdTransform renderFrom[nm108::MaxActors][16]={},renderTo[nm108::MaxActors][16]={};bool renderValid[nm108::MaxActors]={};
    double renderTime=0;float renderInterval=1.f/60.f;
    float solverInterval=1.f/60.f;
    uint32_t fragments[nm108::MaxFragments]={},fragmentGeneration=0;
    RdTransform fragmentFrom[nm108::MaxFragments]={},fragmentTo[nm108::MaxFragments]={};
    bool fragmentRenderValid[nm108::MaxFragments]={};
    std::vector<nm108::Hit> hits;
    // Keep every uploaded face, but share native BVHs between nearby small
    // brushes. Thousands of separate static instances otherwise dominate
    // RAGE's contact search in a full crowd. Large surfaces stay separate.
    std::vector<nm108::Mesh> geometry;
    std::map<std::array<int,3>,size_t> geometryCells;
    bool geometryReady=false;
    bool OpenEmptyRuntime(){
        for(;;){
            std::string log=mapRestarts?workerLog.substr(0,workerLog.size()-4)+".retry-"+std::to_string(mapRestarts)+".log":workerLog;
            if(client.Open(workerPath.c_str(),log.c_str())){
                memcpy(rest,client.State()->poses,sizeof(rest));rig=*client.Skeleton();
                for(unsigned p=1;p<21;++p){V anchor={rig.anchor[p][0],rig.anchor[p][1],rig.anchor[p][2]};
                    restAnchor[p]=Rot(Inv(MatrixQ(rest[p])),anchor-Position(rest[p]));}
                nm108::Request remove={};remove.command=static_cast<unsigned>(nm108::Command::RemoveActor);
                if(Submit(remove))return true;
            }
            client.Close();pendingSteps=0;failed=false;
            if(mapRestarts>=3){failed=true;return false;}
            ++mapRestarts;
        }
    }
    bool SendGeometry(const nm108::Mesh& mesh){
        nm108::Request request={};request.command=static_cast<unsigned>(nm108::Command::AddMesh);request.mesh=mesh;
        if(!Submit(request))return false;
        if(mapCache){fwrite(&mesh,sizeof(mesh),1,mapCache);fflush(mapCache);}return true;
    }
    bool QueueGeometry(const nm108::Mesh& mesh){
        if(geometryReady)return SendGeometry(mesh);
        V lo={1e9f,1e9f,1e9f},hi={-1e9f,-1e9f,-1e9f};
        for(unsigned i=0;i<mesh.vertices;++i){const float* p=mesh.points[i];
            lo.x=(std::min)(lo.x,p[0]);lo.y=(std::min)(lo.y,p[1]);lo.z=(std::min)(lo.z,p[2]);
            hi.x=(std::max)(hi.x,p[0]);hi.y=(std::max)(hi.y,p[1]);hi.z=(std::max)(hi.z,p[2]);}
        const bool compactMesh=hi.x-lo.x<=6.f&&hi.y-lo.y<=6.f&&hi.z-lo.z<=6.f;
        std::array<int,3> key={int(floorf(mesh.transform[12]/4.f)),int(floorf(mesh.transform[13]/4.f)),int(floorf(mesh.transform[14]/4.f))};
        auto found=geometryCells.find(key);
        if(compactMesh&&found!=geometryCells.end()){
            auto& batch=geometry[found->second];
            if(batch.vertices+mesh.vertices<=nm108::MaxVertices&&batch.triangles+mesh.triangles<=nm108::MaxTriangles){
                unsigned base=batch.vertices;
                for(unsigned i=0;i<mesh.vertices;++i){auto* out=batch.points[batch.vertices++];
                    for(unsigned a=0;a<3;++a)out[a]=mesh.points[i][a]+mesh.transform[12+a]-batch.transform[12+a];out[3]=0;}
                for(unsigned t=0;t<mesh.triangles;++t){auto* out=batch.indices[batch.triangles++];
                    for(unsigned a=0;a<3;++a)out[a]=static_cast<uint16_t>(mesh.indices[t][a]+base);}
                return true;
            }
        }
        if(compactMesh)geometryCells[key]=geometry.size();geometry.push_back(mesh);return true;
    }
    bool FlushGeometry(){
        if(geometryReady)return true;
        for(;;){
            bool complete=true;
            for(const auto& mesh:geometry)if(!SendGeometry(mesh)){complete=false;break;}
            if(complete)break;
            // No gameplay state exists during map construction. If the
            // isolated original runtime exits here, rebuild the WHOLE map in
            // a fresh worker rather than silently disabling all hit physics.
            // Never restart a live articulation or resume a partial map.
            for(const auto& a:actors)if(a.created)return false;
            if(mapRestarts>=3||workerPath.empty())return false;
            client.Close();pendingSteps=0;failed=false;
            ++mapRestarts;
            if(!OpenEmptyRuntime())return false;
            if(mapCache){fflush(mapCache);if(_chsize_s(_fileno(mapCache),0)||fseek(mapCache,0,SEEK_SET)){failed=true;return false;}}
        }
        std::vector<nm108::Mesh>().swap(geometry);geometryCells.clear();geometryReady=true;return true;
    }
    void Completed(unsigned steps){
        solverInterval=steps/60.f;
        double time=ClockMs();
        renderInterval=renderTime?(std::max)(1.f/60.f,(std::min)(.05f,float(time-renderTime)*.001f)):steps/60.f;
        renderTime=time;
        for(unsigned i=0;i<nm108::MaxFragments;++i)if(fragments[i]&&!fragmentPending[i]&&client.Fragments()[i].present){
            const auto* pose=client.Fragments()[i].pose;RdTransform next={Host(Position(pose),units),Host(MatrixQ(pose))};
            fragmentFrom[i]=fragmentRenderValid[i]?fragmentTo[i]:next;fragmentTo[i]=next;fragmentRenderValid[i]=true;
        }
        for(unsigned id=0;id<nm108::MaxActors;++id){auto& a=actors[id];if(!a.allocated||!a.created)continue;
            const auto* bodies=Bodies(id);
            for(unsigned p=0;p<16;++p){RdTransform next=bodies[p];
                renderFrom[id][p]=renderValid[id]?renderTo[id][p]:next;renderTo[id][p]=next;}
            renderValid[id]=true;
            a.quiet+=steps/60.f;a.pain*=expf(-float(steps)/30.f);const auto& s=client.Actors()[id];
            if(client.State()->activeControllers&0x40000000u){a.animationReady=a.settling=false;continue;}
            // Stop requesting forward lean during the final support recovery.
            // The host resumes locomotion only after capture-point checks.
            a.shotPressure=a.shotPressure*expf(-float(steps)/(60.f*.45f));
            // Mere side contact is not loss of balance. Support/capture-point
            // tests already reject a body riding on another body or falling.
            a.settling=nm108::CanSettleLocomotion(s);
            const bool crawl=(s.detachedMask&((1u<<2)|(1u<<3)|(1u<<5)|(1u<<6)))!=0;
            const bool safe=nm108::SafeLocomotion(s);
            const bool maintained=crawl?safe:nm108::MaintainLocomotion(s);
            // A completed rise has already reached standing under load.
            // Let its correction steps count if the capture point remains
            // inside support. A fresh hit still requires new evidence.
            const bool stableSupport=safe||(s.recoveryCompleted>a.recoveryAtImpact&&maintained);
            if(a.quiet<.8f||!maintained)a.animationReady=false;
            // Count observed poses rather than unseen catch-up frames. Small
            // corrections decay evidence instead of restarting a rigid timer.
            if(a.quiet>=.8f)a.stable=stableSupport?a.stable+float(steps)/60.f:(std::max)(0.f,a.stable-float(steps)/30.f);
            else a.stable=0;
            if(maintained&&a.stable>.35f)a.animationReady=true;
        }
    }
    bool Flush(){if(!client.Pending())return true;nm108::Response response={};
        if(!client.Finish(response)||response.status!=static_cast<unsigned>(nm108::Status::Complete)){failed=true;return false;}
        Completed(pendingSteps);pendingSteps=0;return true;}
    bool Submit(nm108::Request& request) {
        nm108::Response response={};
        if(!Flush())return false;
        if(!client.Submit(request,response) || response.status!=static_cast<unsigned>(nm108::Status::Complete)){client.ReportFailure("submit");failed=true;return false;}return true;
    }
    // A slow solver is still alive. Deaths, gibs and releases join its ordered
    // lifecycle instead of blocking the render thread and hitting a false
    // 2.5-second failure timeout while another application saturates the CPU.
    bool Enqueue(nm108::Request& request){
        if(failed)return false;
        if(!client.Pending()&&lifecycle.empty())return Submit(request);
        if(lifecycle.size()>=512)return false;
        lifecycle.push_back(request);return true;
    }
    bool DrainLifecycle(){
        for(auto& request:lifecycle){
            if(!Submit(request))return false;
            if(request.command==unsigned(nm108::Command::RemoveActor))actors[request.actor].retired=false;
            if(request.command==unsigned(nm108::Command::CreateFragment))fragmentPending[request.actor]=false;
        }
        lifecycle.clear();return true;
    }
    BridgeActor* Find(uint32_t handle){unsigned slot=handle&31;return slot<nm108::MaxActors&&actors[slot].allocated&&actors[slot].handle==handle?&actors[slot]:nullptr;}
    const BridgeActor* Find(uint32_t handle) const {return const_cast<RdWorld*>(this)->Find(handle);}
    const float* Pose(unsigned slot,unsigned part) const {return actors[slot].created?client.Actors()[slot].poses[part]:actors[slot].initial.pose[part];}
};
namespace {
bool Upload(RdWorld* w,const std::vector<V>& points,const std::vector<std::array<unsigned,3>>& triangles){
    if(w->failed || points.empty() || triangles.empty()) return false;
    nm108::Request mesh={};mesh.command=static_cast<unsigned>(nm108::Command::AddMesh);
    mesh.mesh.transform[0]=mesh.mesh.transform[5]=mesh.mesh.transform[10]=1;
    std::vector<int> remap(points.size(),-1);
    auto flush=[&](){
        // Give every bound local geometry and its actual instance transform.
        // Centering also keeps compressed BVH vertices precise across a map.
        V lo={1e9f,1e9f,1e9f},hi={-1e9f,-1e9f,-1e9f};
        for(unsigned i=0;i<mesh.mesh.vertices;++i){auto* p=mesh.mesh.points[i];lo.x=(std::min)(lo.x,p[0]);lo.y=(std::min)(lo.y,p[1]);lo.z=(std::min)(lo.z,p[2]);hi.x=(std::max)(hi.x,p[0]);hi.y=(std::max)(hi.y,p[1]);hi.z=(std::max)(hi.z,p[2]);}
        V center=(lo+hi)*.5f;mesh.mesh.transform[12]=center.x;mesh.mesh.transform[13]=center.y;mesh.mesh.transform[14]=center.z;
        for(unsigned i=0;i<mesh.mesh.vertices;++i){auto* p=mesh.mesh.points[i];p[0]-=center.x;p[1]-=center.y;p[2]-=center.z;}
        bool ok=w->QueueGeometry(mesh.mesh);
        mesh.mesh.vertices=mesh.mesh.triangles=0;std::fill(remap.begin(),remap.end(),-1);return ok;
    };
    for(auto t:triangles){
        if(t[0]>=points.size()||t[1]>=points.size()||t[2]>=points.size()) {w->failed=true;return false;}
        V cross=Cross(points[t[1]]-points[t[0]],points[t[2]]-points[t[0]]);
        if(Dot(cross,cross)<1e-12f) continue;
        unsigned extra=0;for(auto index:t) if(remap[index]<0) ++extra;
        if(mesh.mesh.vertices+extra>nm108::MaxVertices || mesh.mesh.triangles==nm108::MaxTriangles) if(!flush())return false;
        auto& polygon=mesh.mesh.indices[mesh.mesh.triangles++];
        for(unsigned k=0;k<3;++k){unsigned index=t[k];if(remap[index]<0){remap[index]=mesh.mesh.vertices++;auto p=points[index];auto* v=mesh.mesh.points[remap[index]];v[0]=p.x;v[1]=p.y;v[2]=p.z;}polygon[k]=static_cast<uint16_t>(remap[index]);}
    }
    return !mesh.mesh.triangles || flush();
}
// A small convex brush hull: identify supporting planes, remove coplanar
// interior points with a 2D monotone hull, then triangulate each boundary face.
bool Hull(const std::vector<V>& points,std::vector<std::array<unsigned,3>>& triangles){
    if(points.size()<4 || points.size()>128) return false;
    struct Plane {V n;float d;};std::vector<Plane> planes;V center={};for(auto p:points) center=center+p;center=center*(1.f/points.size());
    const float epsilon=1e-5f;
    for(unsigned a=0;a<points.size();++a)for(unsigned b=a+1;b<points.size();++b)for(unsigned c=b+1;c<points.size();++c){
        V n=Cross(points[b]-points[a],points[c]-points[a]);float length=sqrtf(Dot(n,n));if(length<epsilon)continue;n=n*(1.f/length);float d=Dot(n,points[a]);
        bool positive=false,negative=false;for(auto p:points){float v=Dot(n,p)-d;positive|=v>epsilon;negative|=v< -epsilon;}
        if(positive&&negative || !positive&&!negative)continue;
        if(Dot(n,center)>d){n=n*-1;d=-d;}
        bool duplicate=false;for(auto plane:planes)if(Dot(n,plane.n)>1-1e-5f && fabsf(d-plane.d)<epsilon*2)duplicate=true;
        if(!duplicate)planes.push_back({n,d});
    }
    for(auto plane:planes){
        V basis=fabsf(plane.n.z)<.8f?V{0,0,1}:V{0,1,0};V u=Cross(basis,plane.n);u=u*(1/sqrtf(Dot(u,u)));V v=Cross(plane.n,u);
        struct P {float x,y;unsigned id;};std::vector<P> face;
        for(unsigned i=0;i<points.size();++i)if(fabsf(Dot(plane.n,points[i])-plane.d)<epsilon*3)face.push_back({Dot(points[i],u),Dot(points[i],v),i});
        std::sort(face.begin(),face.end(),[](P a,P b){return a.x<b.x || a.x==b.x&&a.y<b.y;});
        auto turn=[](P a,P b,P c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
        std::vector<P> hull;
        for(auto p:face){while(hull.size()>=2 && turn(hull[hull.size()-2],hull.back(),p)<=epsilon*epsilon)hull.pop_back();hull.push_back(p);}
        size_t lower=hull.size();
        for(int i=static_cast<int>(face.size())-2;i>=0;--i){auto p=face[i];while(hull.size()>lower && turn(hull[hull.size()-2],hull.back(),p)<=epsilon*epsilon)hull.pop_back();hull.push_back(p);}
        if(hull.size()<4)continue;hull.pop_back();for(unsigned i=1;i+1<hull.size();++i)triangles.push_back({hull[0].id,hull[i].id,hull[i+1].id});
    }
    return planes.size()>=4 && triangles.size()>=4;
}
V Anchor(const RdWorld* w,unsigned slot,unsigned part){if(!part)return Position(w->Pose(slot,0));return Position(w->Pose(slot,part))+Rot(MatrixQ(w->Pose(slot,part)),w->restAnchor[part]);}
}
API RdWorld* rd_world_create_ex(const RdWorldDesc* desc){
    if(!desc || desc->axes!=RD_AXES_Z_UP || !std::isfinite(desc->units_per_meter) || desc->units_per_meter<=0) return nullptr;
    char directory[MAX_PATH]={};HMODULE module=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(&rd_world_create_ex),&module))return nullptr;
    if(!GetModuleFileNameA(module,directory,MAX_PATH))return nullptr;char* slash=strrchr(directory,'\\');if(!slash)return nullptr;*slash=0;
    auto* w=new RdWorld;w->units=desc->units_per_meter;
    std::string worker=std::string(directory)+"\\nm108_worker.exe";std::string log=std::string(directory)+"\\nm108-native-worker-"+std::to_string(GetCurrentProcessId())+".log";
    char backend[32]={};GetEnvironmentVariableA("BO1_PHYSICS_BACKEND",backend,sizeof(backend));
    const std::string delirium=std::string(directory)+"\\delirium_worker.exe";
    if(_stricmp(backend,"native")&&GetFileAttributesA(delirium.c_str())!=INVALID_FILE_ATTRIBUTES){
        worker=delirium;log=std::string(directory)+"\\delirium-worker-"+std::to_string(GetCurrentProcessId())+".log";}
    char logDirectory[32768]{};
    if(GetEnvironmentVariableA("BO1_DELIRIUM_LOG_DIR",logDirectory,sizeof(logDirectory)))
        log=std::string(logDirectory)+"\\delirium-worker-"+std::to_string(GetCurrentProcessId())+".log";
    w->workerPath=worker;w->workerLog=log;
    if(!w->OpenEmptyRuntime()){delete w;return nullptr;}
    // Explicit local diagnostic cache: numeric map geometry only, never
    // enabled by normal play, never stored in a public source directory.
    char cache[MAX_PATH]={};if(GetEnvironmentVariableA("NM108_PRIVATE_MAP_CACHE",cache,sizeof(cache)))fopen_s(&w->mapCache,cache,"wb");
    if(GetEnvironmentVariableA("NM108_PRIVATE_INITIAL_CACHE",cache,sizeof(cache)))fopen_s(&w->initialCache,cache,"wb");
    return w;
}
API void rd_world_destroy(RdWorld* w){delete w;}
API uint32_t rd_world_capabilities(const RdWorld* w){return w&&!w->failed&&w->client.State()?((w->client.State()->activeControllers>>31)&1u)|((w->client.State()->activeControllers>>29)&2u):0u;}
// Complete Delirium message interface. Unknown/invalid messages are rejected
// by the worker without mapping the simulator into BO1's address space.
API int32_t rd_actor_message(RdWorld* w,uint32_t actor,const char* text){
    auto* a=w?w->Find(actor):nullptr;if(!a||!a->created||!text||!(rd_world_capabilities(w)&1u))return RD_ERR_ARG;
    const size_t size=strnlen_s(text,sizeof(nm108::Advance));if(!size||size>=sizeof(nm108::Advance))return RD_ERR_ARG;
    nm108::Request r={};r.command=14;r.actor=actor&31;memcpy(&r.advance,text,size+1);
    if(!w->Flush())return RD_ERR_LOAD;nm108::Response response={};
    if(!w->client.Submit(r,response)){w->failed=true;return RD_ERR_LOAD;}
    return response.status==unsigned(nm108::Status::Complete)?RD_OK:RD_ERR_ARG;
}
API int32_t rd_world_set_async(RdWorld* w,uint32_t enabled){
    if(!w||enabled>1||!w->Flush())return RD_ERR_ARG;
    // BO1 switches to asynchronous stepping after UploadMap: finish the BVHs
    // here during world initialization, rather than on the first window entry.
    if(!w->geometry.empty()&&!w->FlushGeometry())return RD_ERR_LOAD;
    w->asynchronous=enabled!=0;return RD_OK;
}
API int32_t rd_world_set_params_ron(RdWorld* w,const char*){return w?RD_ERR_ARG:RD_ERR_NOT_FOUND;}
API int32_t rd_load_art(RdWorld* w,const char* path,uint32_t* model){
    // The worker has loaded the original character from the local disc cache.
    if(!w||!path||!model||!w->client.State()||w->client.State()->parts!=21)return RD_ERR_ARG;
    *model=0;return RD_OK;
}
// BO1 pairs its bones with these bodies through anatomical frames built from
// fixed axes (forward, left), so the rest it receives must face GTA's +Y like
// the original runtime's. Delirium's Fred build faces -Y: without this turn
// every limb was paired rolled about its own axis (arms up to 111 degrees)
// and twisted on entering physics. Only these two exports turn; the runtime's
// own rest stays as built (nm108_rest_facing_verify.cpp).
Q RestFacing(const RdWorld* w){
    V left={w->rig.anchor[1][0]-w->rig.anchor[4][0],w->rig.anchor[1][1]-w->rig.anchor[4][1],0};
    V forward=Cross(left,{0,0,1});if(Dot(forward,forward)<1e-6f)return {0,0,0,1};
    float turn=1.5707963f-atan2f(forward.y,forward.x);return {0,0,sinf(turn*.5f),cosf(turn*.5f)};
}
API int32_t rd_model_rest_bodies(const RdWorld* w,uint32_t model,RdTransform* out){if(!w||model||!out)return RD_ERR_ARG;const Q turn=RestFacing(w);for(unsigned i=0;i<16;++i){auto* m=w->rest[parts16[i]];out[i]={Host(Rot(turn,Position(m)-Position(w->rest[0])),w->units),Host(Mul(turn,MatrixQ(m)))};}return RD_OK;}
API int32_t rd_model_rest_segments(const RdWorld* w,uint32_t model,RdVec3* starts,RdVec3* ends){
    if(!w||model||!starts||!ends)return RD_ERR_ARG;
    const unsigned child[16]={7,2,3,3,5,6,6,10,11,12,15,16,16,19,20,20};
    for(unsigned i=0;i<16;++i){unsigned p=parts16[i];V a=p?V{w->rig.anchor[p][0],w->rig.anchor[p][1],w->rig.anchor[p][2]}:Position(w->rest[0]);V b={w->rig.anchor[child[i]][0],w->rig.anchor[child[i]][1],w->rig.anchor[child[i]][2]};if(child[i]==p)b=Position(w->rest[p])*2-a;const Q turn=RestFacing(w);starts[i]=Host(Rot(turn,a-Position(w->rest[0])),w->units);ends[i]=Host(Rot(turn,b-Position(w->rest[0])),w->units);}return RD_OK;
}
API int32_t rd_add_static_mesh(RdWorld* w,const float* vertices,uint32_t count,const uint32_t* indices,uint32_t triangles){
    if(!w||!vertices||!indices||!count||!triangles||count>1000000||triangles>2000000)return RD_ERR_ARG;
    std::vector<V> points;points.reserve(count);for(unsigned i=0;i<count;++i){V p=Gta({vertices[i*3],vertices[i*3+1],vertices[i*3+2]},w->units);if(!Finite(p))return RD_ERR_ARG;points.push_back(p);}
    std::vector<std::array<unsigned,3>> faces;faces.reserve(triangles);for(unsigned i=0;i<triangles;++i)faces.push_back({indices[i*3],indices[i*3+1],indices[i*3+2]});return Upload(w,points,faces)?RD_OK:RD_ERR_LOAD;
}
API int32_t rd_add_static_convex(RdWorld* w,const float* vertices,uint32_t count){
    if(!w||!vertices||count<4||count>128)return RD_ERR_ARG;std::vector<V> points;
    for(unsigned i=0;i<count;++i){V p=Gta({vertices[i*3],vertices[i*3+1],vertices[i*3+2]},w->units);if(!Finite(p))return RD_ERR_ARG;points.push_back(p);}
    std::vector<std::array<unsigned,3>> faces;if(!Hull(points,faces))return RD_ERR_ARG;return Upload(w,points,faces)?RD_OK:RD_ERR_LOAD;
}
API int32_t rd_actor_spawn(RdWorld* w,uint32_t model,const RdTransform* root,RdVec3 velocity,uint32_t* out){
    if(!w||model||!root||!out||w->failed||!Finite(Gta(root->position,w->units))||!Finite(Gta(root->rotation))||!Finite(Gta(velocity,w->units)))return RD_ERR_ARG;
    if(!w->FlushGeometry())return RD_ERR_LOAD;
    for(unsigned slot=0;slot<nm108::MaxActors;++slot)if(!w->actors[slot].allocated&&!w->actors[slot].retired){auto& a=w->actors[slot];a={};w->renderValid[slot]=false;a.allocated=true;a.handle=(++w->generation<<5)|slot;
        Q yaw=Gta(root->rotation);V p=Gta(root->position,w->units);Q restRoot=MatrixQ(w->rest[0]);
        Q delta=Mul(yaw,Inv(restRoot));for(unsigned part=0;part<21;++part){Matrix(Mul(delta,MatrixQ(w->rest[part])),p+Rot(delta,Position(w->rest[part])-Position(w->rest[0])),a.initial.pose[part]);V v=Gta(velocity,w->units);a.initial.linearVelocity[part][0]=v.x;a.initial.linearVelocity[part][1]=v.y;a.initial.linearVelocity[part][2]=v.z;}*out=a.handle;return RD_OK;}return RD_ERR_LOAD;
}
API int32_t rd_actor_set_pose_rotations(RdWorld* w,uint32_t actor,const RdTransform* pelvis,const RdQuat* rotations,RdVec3 velocity){
    auto* a=w?w->Find(actor):nullptr;if(!a||a->created||!pelvis||!rotations)return RD_ERR_ARG;
    Q q[21];for(unsigned part=0;part<21;++part)q[part]=MatrixQ(w->rest[part]);
    for(unsigned i=0;i<16;++i){q[parts16[i]]=Gta(rotations[i]);if(!Finite(q[parts16[i]]))return RD_ERR_ARG;}
    Q lowerDelta=Mul(q[7],Inv(MatrixQ(w->rest[7]))),upperDelta=Mul(q[10],Inv(MatrixQ(w->rest[10]))),headDelta=Mul(q[12],Inv(MatrixQ(w->rest[12])));
    q[8]=Mul(Mix(lowerDelta,upperDelta,.33f),MatrixQ(w->rest[8]));
    q[9]=Mul(Mix(lowerDelta,upperDelta,.67f),MatrixQ(w->rest[9]));
    q[11]=Mul(Mix(upperDelta,headDelta,.67f),MatrixQ(w->rest[11]));
    for(unsigned part:{13u,17u})q[part]=Mul(Mul(q[10],Inv(MatrixQ(w->rest[10]))),MatrixQ(w->rest[part]));
    V p[21];p[0]=Gta(pelvis->position,w->units);V v=Gta(velocity,w->units);if(!Finite(p[0])||!Finite(v))return RD_ERR_ARG;
    for(unsigned part=0;part<21;++part){if(part){int parent=w->rig.parent[part];V anchor={w->rig.anchor[part][0],w->rig.anchor[part][1],w->rig.anchor[part][2]};p[part]=p[parent]+Rot(q[parent],Rot(Inv(MatrixQ(w->rest[parent])),anchor-Position(w->rest[parent])))-Rot(q[part],Rot(Inv(MatrixQ(w->rest[part])),anchor-Position(w->rest[part])));}Matrix(q[part],p[part],a->initial.pose[part]);a->initial.linearVelocity[part][0]=v.x;a->initial.linearVelocity[part][1]=v.y;a->initial.linearVelocity[part][2]=v.z;}return RD_OK;
}
API int32_t rd_actor_set_bodies(RdWorld* w,uint32_t actor,const RdTransform* bodies,const RdVec3* linear,const RdVec3* angular){
    auto* a=w?w->Find(actor):nullptr;if(!a||a->created||!bodies)return RD_ERR_ARG;
    RdQuat rotations[16];for(unsigned i=0;i<16;++i)rotations[i]=bodies[i].rotation;
    int result=rd_actor_set_pose_rotations(w,actor,bodies,rotations,linear?linear[0]:RdVec3{});if(result)return result;
    V omega[21]={},vel[21]={};
    for(unsigned i=0;i<16;++i){omega[parts16[i]]=angular?Gta(angular[i],1):V{};
        if(!Finite(omega[parts16[i]])||(linear&&!Finite(Gta(linear[i],w->units))))return RD_ERR_ARG;}
    // These intermediate spine/neck/shoulder frames use the same interpolation
    // as set_pose_rotations; they must not inherit the pelvis' angular velocity.
    omega[8]=omega[7]*.67f+omega[10]*.33f;omega[9]=omega[7]*.33f+omega[10]*.67f;
    omega[11]=omega[10]*.33f+omega[12]*.67f;omega[13]=omega[17]=omega[10];
    vel[0]=linear?Gta(linear[0],w->units):V{};
    // Different BO1/Fred bone lengths require a kinematic projection. Preserve
    // root translation and every joint's measured angular motion, and make the
    // velocities agree at shared anchors (otherwise the first solve kicks limbs).
    for(unsigned part=0;part<21;++part){
        if(part){const unsigned parent=w->rig.parent[part];
            V anchor={w->rig.anchor[part][0],w->rig.anchor[part][1],w->rig.anchor[part][2]};
            V parentArm=Rot(MatrixQ(a->initial.pose[parent]),Rot(Inv(MatrixQ(w->rest[parent])),anchor-Position(w->rest[parent])));
            V childArm=Rot(MatrixQ(a->initial.pose[part]),Rot(Inv(MatrixQ(w->rest[part])),anchor-Position(w->rest[part])));
            vel[part]=vel[parent]+Cross(omega[parent],parentArm)-Cross(omega[part],childArm);
        }
        auto* lv=a->initial.linearVelocity[part];lv[0]=vel[part].x;lv[1]=vel[part].y;lv[2]=vel[part].z;
        auto* av=a->initial.angularVelocity[part];av[0]=omega[part].x;av[1]=omega[part].y;av[2]=omega[part].z;
    }return RD_OK;
}
API int32_t rd_actor_get_box_vertices(const RdWorld* w,uint32_t actor,unsigned body,RdVec3* vertices){
    const auto* a=w?w->Find(actor):nullptr;if(!a||body>=16||!vertices)return RD_ERR_ARG;
    const auto shape=BirthClearance::RecoveryGeometry(w->Pose(actor&31,parts16[body]),w->rig.shapes[parts16[body]]);
    if(!shape.box)return RD_ERR_ARG;
    for(unsigned i=0;i<8;++i){V point=shape.center;
        point=point+shape.axis[0]*(shape.half.x*((i&1)?1.f:-1.f));
        point=point+shape.axis[1]*(shape.half.y*((i&2)?1.f:-1.f));
        point=point+shape.axis[2]*(shape.half.z*((i&4)?1.f:-1.f));vertices[i]=Host(point,w->units);}
    return RD_OK;
}
API int32_t rd_actor_set_mode(RdWorld* w,uint32_t actor,uint32_t mode){
    auto* a=w?w->Find(actor):nullptr;if(!a||mode>1||w->failed)return RD_ERR_ARG;
    nm108::Request request={};request.actor=actor&31;
    if(!a->created){request.command=static_cast<unsigned>(nm108::Command::CreateActor);request.initial=a->initial;
        if(w->initialCache){fwrite(&actor,sizeof(actor),1,w->initialCache);fwrite(&a->initial,sizeof(a->initial),1,w->initialCache);fflush(w->initialCache);}
        if(!w->Submit(request))return RD_ERR_LOAD;a->created=true;w->lastBirthFrame=w->client.State()->frame;
        if(a->detachedMask){request={};request.command=unsigned(nm108::Command::SetDetachedParts);request.actor=actor&31;
            for(unsigned i=0;i<16;++i)if(a->detachedMask&(1u<<i))request.active|=1u<<parts16[i];
            if(!w->Submit(request))return RD_ERR_LOAD;}
        if(a->gravity!=1000){request={};request.command=unsigned(nm108::Command::SetGravity);request.actor=actor&31;request.active=a->gravity;
            if(!w->Submit(request))return RD_ERR_LOAD;}}
    request={};request.command=static_cast<unsigned>(nm108::Command::SetMode);request.actor=actor&31;request.active=mode;
    if(!w->Enqueue(request))return RD_ERR_LOAD;a->mode=mode;return RD_OK;
}
API int32_t rd_actor_despawn(RdWorld* w,uint32_t actor){auto* a=w?w->Find(actor):nullptr;if(!a)return RD_ERR_NOT_FOUND;if(a->created){nm108::Request remove={};remove.command=static_cast<unsigned>(nm108::Command::RemoveActor);remove.actor=actor&31;bool deferred=w->client.Pending()||!w->lifecycle.empty();if(!w->Enqueue(remove))return RD_ERR_LOAD;a->retired=deferred;}a->allocated=false;w->hits.erase(std::remove_if(w->hits.begin(),w->hits.end(),[&](const nm108::Hit& hit){return hit.actor==(actor&31);}),w->hits.end());return RD_OK;}
API int32_t rd_actor_get_bodies(const RdWorld* w,uint32_t actor,RdTransform* out,uint32_t capacity){auto* a=w?w->Find(actor):nullptr;if(!a||!out||capacity<16||w->failed)return RD_ERR_ARG;memcpy(out,w->Bodies(actor&31),sizeof(RdTransform)*16);return RD_OK;}
// Moon: this body's gravity as a fraction of Earth's (zombie_moon's low
// gravity is setplayergravity 136 against 800). Sent only when it changes.
API int32_t rd_actor_set_gravity(RdWorld* w,uint32_t actor,float scale){
    auto* a=w?w->Find(actor):nullptr;if(!a||!std::isfinite(scale)||scale<0||scale>1)return RD_ERR_ARG;
    const uint32_t milli=uint32_t(scale*1000.f+.5f);if(milli==a->gravity)return RD_OK;a->gravity=milli;
    if(!a->created)return RD_OK;
    nm108::Request request={};request.command=unsigned(nm108::Command::SetGravity);request.actor=actor&31;request.active=milli;
    return w->Enqueue(request)?RD_OK:RD_ERR_LOAD;
}
API int32_t rd_actor_set_detached_parts(RdWorld* w,uint32_t actor,uint32_t mask){
    auto* a=w?w->Find(actor):nullptr;if(!a||(mask&~0xffffu)||(a->detachedMask&~mask))return RD_ERR_ARG;
    if(!a->created){a->detachedMask=mask;return RD_OK;}
    nm108::Request request={};request.command=unsigned(nm108::Command::SetDetachedParts);request.actor=actor&31;
    for(unsigned i=0;i<16;++i)if(mask&(1u<<i))request.active|=1u<<parts16[i];
    if(!w->Enqueue(request))return RD_ERR_LOAD;a->detachedMask=mask;a->stable=0;a->quiet=0;a->settling=a->animationReady=false;
    return RD_OK;
}
API int32_t rd_actor_get_body_velocities(const RdWorld* w,uint32_t actor,RdVec3* linear,RdVec3* angular){
    if(!w||!w->Find(actor)||!linear||!angular)return RD_ERR_ARG;unsigned id=actor&31;
    for(unsigned i=0;i<16;++i){linear[i]=angular[i]={};if(!w->renderValid[id])continue;
        const auto& a=w->renderFrom[id][i];const auto& b=w->renderTo[id][i];float rate=1.f/w->solverInterval;
        linear[i]={(b.position.x-a.position.x)*rate,(b.position.y-a.position.y)*rate,(b.position.z-a.position.z)*rate};
        Q d=Mul({b.rotation.x,b.rotation.y,b.rotation.z,b.rotation.w},Inv({a.rotation.x,a.rotation.y,a.rotation.z,a.rotation.w}));if(d.w<0)d={-d.x,-d.y,-d.z,-d.w};
        float s=sqrtf(d.x*d.x+d.y*d.y+d.z*d.z),gain=s>1e-5f?2.f*atan2f(s,d.w)*rate/s:2.f*rate;
        angular[i]={d.x*gain,d.y*gain,d.z*gain};
    }return RD_OK;
}
API int32_t rd_model_body_masses(const RdWorld* w,float* masses){if(!w||!masses)return RD_ERR_ARG;
    for(unsigned i=0;i<16;++i)masses[i]=w->rig.shapes[parts16[i]].mass;
    if(rd_world_capabilities(w)&1u)for(unsigned part=0;part<21;++part){bool mapped=false;for(unsigned i=0;i<16;++i)mapped|=parts16[i]==part;if(mapped)continue;
        int parent=w->rig.parent[part];while(parent>=0){bool found=false;for(unsigned i=0;i<16;++i)if(parts16[i]==unsigned(parent)){masses[i]+=w->rig.shapes[part].mass;found=true;break;}if(found)break;parent=w->rig.parent[parent];}}
    return RD_OK;}
API int32_t rd_fragment_spawn(RdWorld* w,const RdFragmentDesc* desc,uint32_t* out){
    if(!w||!desc||!out||w->failed||!Finite(Gta(desc->transform.rotation)))return RD_ERR_ARG;
    for(unsigned id=0;id<nm108::MaxFragments;++id)if(!w->fragments[id]){
        nm108::Request request={};request.command=unsigned(nm108::Command::CreateFragment);request.actor=id;
        Matrix(Gta(desc->transform.rotation),Gta(desc->transform.position,w->units),request.fragment.pose);
        V linear=Gta(desc->linear,w->units),angular=Gta(desc->angular,1);
        memcpy(request.fragment.linear,&linear,12);memcpy(request.fragment.angular,&angular,12);
        request.fragment.dimensions[0]=desc->dimensions.x/w->units;request.fragment.dimensions[1]=desc->dimensions.y/w->units;request.fragment.dimensions[2]=desc->dimensions.z/w->units;
        bool deferred=w->client.Pending()||!w->lifecycle.empty();
        request.fragment.mass=desc->mass;if(!w->Enqueue(request))return RD_ERR_LOAD;
        w->fragmentPending[id]=deferred;
        *out=w->fragments[id]=(++w->fragmentGeneration<<8)|id;
        w->fragmentFrom[id]=w->fragmentTo[id]=desc->transform;w->fragmentRenderValid[id]=true;return RD_OK;
    }return RD_ERR_LOAD;
}
API int32_t rd_fragment_despawn(RdWorld* w,uint32_t handle){
    unsigned id=handle&255;if(!w||id>=nm108::MaxFragments||w->fragments[id]!=handle)return RD_ERR_NOT_FOUND;
    nm108::Request request={};request.command=unsigned(nm108::Command::RemoveFragment);request.actor=id;
    if(!w->Enqueue(request))return RD_ERR_LOAD;w->fragments[id]=0;w->fragmentRenderValid[id]=false;return RD_OK;
}
API int32_t rd_fragment_get_transform(const RdWorld* w,uint32_t handle,RdTransform* out){
    unsigned id=handle&255;if(!w||!out||id>=nm108::MaxFragments||w->fragments[id]!=handle)return RD_ERR_NOT_FOUND;
    float t=(std::min)(1.f,float(ClockMs()-w->renderTime)*.001f/w->renderInterval);
    const auto& a=w->fragmentFrom[id];const auto& b=w->fragmentTo[id];
    out->position={a.position.x+(b.position.x-a.position.x)*t,a.position.y+(b.position.y-a.position.y)*t,a.position.z+(b.position.z-a.position.z)*t};
    Q q=Mix({a.rotation.x,a.rotation.y,a.rotation.z,a.rotation.w},{b.rotation.x,b.rotation.y,b.rotation.z,b.rotation.w},t);out->rotation={q.x,q.y,q.z,q.w};return RD_OK;
}
API int32_t rd_fragment_hit(RdWorld* w,uint32_t handle,RdVec3 point,RdVec3 impulse,float duration){
    unsigned id=handle&255;if(!w||id>=nm108::MaxFragments||w->fragments[id]!=handle||!std::isfinite(duration)||duration<0||duration>.3f)return RD_ERR_ARG;
    V p=Gta(point,w->units),j=Gta(impulse,1);if(!Finite(p)||!Finite(j)||Dot(j,j)>128.f*128.f+.02f)return RD_ERR_ARG;
    nm108::Request request={};request.command=unsigned(nm108::Command::FragmentImpulse);request.actor=id;request.hitCount=1;
    memcpy(request.hits[0].point,&p,12);memcpy(request.hits[0].impulse,&j,12);request.hits[0].impulse[3]=duration;
    return w->Enqueue(request)?RD_OK:RD_ERR_LOAD;
}
// Presentation only: interpolate whole completed snapshots. Collision,
// wounds, get-up eligibility and server movement always read physical poses.
API int32_t rd_actor_get_render_bodies(const RdWorld* w,uint32_t actor,RdTransform* out,uint32_t capacity){
    if(!w||!w->Find(actor)||!out||capacity<16||w->failed)return RD_ERR_ARG;
    unsigned id=actor&31;if(!w->renderValid[id])return rd_actor_get_bodies(w,actor,out,capacity);
    float t=(std::min)(1.f,float(ClockMs()-w->renderTime)*.001f/(std::max)(1.f/60.f,w->renderInterval));
    for(unsigned i=0;i<16;++i){const auto& a=w->renderFrom[id][i];const auto& b=w->renderTo[id][i];
        out[i].position={a.position.x+(b.position.x-a.position.x)*t,a.position.y+(b.position.y-a.position.y)*t,a.position.z+(b.position.z-a.position.z)*t};
        Q q=Mix({a.rotation.x,a.rotation.y,a.rotation.z,a.rotation.w},{b.rotation.x,b.rotation.y,b.rotation.z,b.rotation.w},t);
        out[i].rotation={q.x,q.y,q.z,q.w};}return RD_OK;
}
API int32_t rd_actor_get_joint_positions(const RdWorld* w,uint32_t actor,RdVec3* out){if(!w||!w->Find(actor)||!out||w->failed)return RD_ERR_ARG;unsigned id=actor&31;auto& cache=w->Cache(id);
    if(!cache.joints){for(unsigned i=0;i<16;++i)cache.anchors[i]=Host(Anchor(w,id,parts16[i]),w->units);cache.joints=true;}
    memcpy(out,cache.anchors,sizeof(cache.anchors));return RD_OK;}
// Exact ART collision bounds, rather than ankle bone positions. Available
// before activation so the host can fit the soles to its landing surface.
API int32_t rd_actor_get_bounds(const RdWorld* w,uint32_t actor,RdVec3* mins,RdVec3* maxs){
    if(!w||!w->Find(actor)||!mins||!maxs||w->failed)return RD_ERR_ARG;
    unsigned id=actor&31;auto& cache=w->Cache(id);
    if(cache.bounds){memcpy(mins,cache.mins,sizeof(cache.mins));memcpy(maxs,cache.maxs,sizeof(cache.maxs));return RD_OK;}
    for(unsigned i=0;i<16;++i){unsigned part=parts16[i];const auto& s=w->rig.shapes[part];auto* m=w->Pose(actor&31,part);
        Q q=MatrixQ(m),axes=Mul(q,{s.orientation[0],s.orientation[1],s.orientation[2],s.orientation[3]});
        RdVec3 center=Host(Position(m)+Rot(q,{s.offset[0],s.offset[1],s.offset[2]}),w->units);
        RdVec3 x=Host(Rot(axes,{1,0,0}),1),y=Host(Rot(axes,{0,1,0}),1),z=Host(Rot(axes,{0,0,1}),1),extent;
        if(s.box)extent={fabsf(x.x)*s.size[0]*.5f+fabsf(y.x)*s.size[1]*.5f+fabsf(z.x)*s.size[2]*.5f,
            fabsf(x.y)*s.size[0]*.5f+fabsf(y.y)*s.size[1]*.5f+fabsf(z.y)*s.size[2]*.5f,
            fabsf(x.z)*s.size[0]*.5f+fabsf(y.z)*s.size[1]*.5f+fabsf(z.z)*s.size[2]*.5f};
        else extent={fabsf(y.x)*s.length*.5f+s.radius,fabsf(y.y)*s.length*.5f+s.radius,fabsf(y.z)*s.length*.5f+s.radius};
        extent.x*=w->units;extent.y*=w->units;extent.z*=w->units;
        mins[i]={center.x-extent.x,center.y-extent.y,center.z-extent.z};maxs[i]={center.x+extent.x,center.y+extent.y,center.z+extent.z};
    }memcpy(cache.mins,mins,sizeof(cache.mins));memcpy(cache.maxs,maxs,sizeof(cache.maxs));cache.bounds=true;return RD_OK;
}
API int32_t rd_weapon_response(uint32_t profile,float* impulse,float* duration,float* speed){
    if(!impulse||!duration||!speed)return RD_ERR_ARG;
    static const euphoria::LocalWeaponResponses profiles;
    euphoria::WeaponResponse response={};if(!profiles.get(profile,response))return RD_ERR_ARG;
    *impulse=response.impulse;*duration=response.duration;*speed=response.bulletSpeed;return RD_OK;
}
API int32_t rd_actor_hit_profile(RdWorld* w,uint32_t actor,const RdHit* hit,float duration,float speed,uint32_t profile){
    auto* a=w?w->Find(actor):nullptr;if(!a||!a->created||!hit||hit->body>=16||w->failed)return RD_ERR_ARG;
    if(!std::isfinite(duration)||duration<0||duration>.3f||!std::isfinite(speed)||speed<=0||speed>1500||(profile>=euphoria::WeaponResponseCount&&profile!=nm108::MeleeImpulse))return RD_ERR_ARG;
    V p=Gta(hit->point,w->units),impulse=Gta(hit->impulse,1);float magnitude=sqrtf(Dot(impulse,impulse));if(!Finite(p)||!Finite(impulse)||magnitude>128.0001f)return RD_ERR_ARG;
    if(magnitude>128.f){impulse=impulse*(128.f/magnitude);magnitude=128.f;}
    // Retarget the visual hit to this limb's actual collision volume. Keep
    // that location in limb space while IPC is pending: an old world point
    // otherwise acquires a long, artificial torque arm as the body moves.
    unsigned part=parts16[hit->body];while(a->detachedMask&(1u<<hit->body)){int parent=w->rig.parent[part];if(parent<0)break;part=unsigned(parent);bool absent=false;for(unsigned i=0;i<16;++i)if(parts16[i]==part&&(a->detachedMask&(1u<<i)))absent=true;if(!absent)break;}const auto* pose=w->Pose(actor&31,part);
    const auto& shape=w->rig.shapes[part];Q axes={shape.orientation[0],shape.orientation[1],shape.orientation[2],shape.orientation[3]};
    V offset={shape.offset[0],shape.offset[1],shape.offset[2]};
    V local=Rot(Inv(axes),Rot(Inv(MatrixQ(pose)),p-Position(pose))-offset);
    if(shape.box){float* values=&local.x;for(unsigned j=0;j<3;++j)values[j]=(std::max)(-shape.size[j]*.5f,(std::min)(shape.size[j]*.5f,values[j]));}
    else {V center={0,(std::max)(-shape.length*.5f,(std::min)(shape.length*.5f,local.y)),0};V radial=local-center;float radius=sqrtf(Dot(radial,radial));
        if(radius>shape.radius)local=center+radial*(shape.radius/radius);}
    local=offset+Rot(axes,local);
    nm108::Hit bullet={};bullet.actor=actor&31;bullet.part=part;bullet.point[0]=local.x;bullet.point[1]=local.y;bullet.point[2]=local.z;bullet.point[3]=1;
    bullet.impulse[0]=impulse.x;bullet.impulse[1]=impulse.y;bullet.impulse[2]=impulse.z;
    bullet.impulse[3]=duration;bullet.normal[3]=float(profile);
    if(magnitude>1e-6f){V direction=impulse*(1/magnitude);bullet.bulletVelocity[0]=direction.x*speed;bullet.bulletVelocity[1]=direction.y*speed;bullet.bulletVelocity[2]=direction.z*speed;bullet.normal[0]=-direction.x;bullet.normal[1]=-direction.y;bullet.normal[2]=-direction.z;}
    if(magnitude<=1e-6f)return RD_OK;
    if(w->hits.size()>=4096)return RD_ERR_LOAD;
    w->hits.push_back(bullet);
    float mass=0;for(unsigned p=0;p<21;++p)if(!(w->client.Actors()[actor&31].detachedMask&(1u<<p)))mass+=w->rig.shapes[p].mass;
    if(mass>0)a->shotPressure=a->shotPressure+impulse*(1.f/mass);
    a->recoveryAtImpact=w->client.Actors()[actor&31].recoveryCompleted;
    a->pain+=(std::min)(magnitude,40.f);a->quiet=a->stable=0;a->settling=a->animationReady=false;return RD_OK;
}
// External contact/blast momentum, distinct from a projectile and its NMShot.
API int32_t rd_actor_push(RdWorld* w,uint32_t actor,unsigned body,RdVec3 impulse,float duration,unsigned kind){
    auto* a=w?w->Find(actor):nullptr;if(!a||!a->created||body>=16||w->failed||
        (kind!=nm108::ContactImpulse&&kind!=nm108::ExplosionImpulse)||!std::isfinite(duration)||duration<0||duration>.3f)return RD_ERR_ARG;
    V j=Gta(impulse,1);if(!Finite(j)||Dot(j,j)>128.f*128.f||w->hits.size()>=4096)return RD_ERR_ARG;
    nm108::Hit push={};push.actor=actor&31;push.part=parts16[body];push.point[3]=1;
    push.impulse[0]=j.x;push.impulse[1]=j.y;push.impulse[2]=j.z;push.impulse[3]=duration;push.normal[3]=float(kind);
    w->hits.push_back(push);
    // Ordinary crowd contacts are judged by the resulting support/velocity.
    // Treating every small shove as a new bullet prevented recovery forever.
    if(kind==nm108::ExplosionImpulse||Dot(j,j)>144.f){
        a->recoveryAtImpact=w->client.Actors()[actor&31].recoveryCompleted;
        a->quiet=a->stable=0;a->animationReady=a->settling=false;
    }
    return RD_OK;
}
API int32_t rd_actor_hit(RdWorld* w,uint32_t actor,const RdHit* hit){return rd_actor_hit_profile(w,actor,hit,0,450,0);}
API int32_t rd_actor_set_motion(RdWorld* w,uint32_t actor,RdVec3 velocity,RdVec3 forward){
    auto* a=w?w->Find(actor):nullptr;if(!a||w->failed||!Finite(Gta(velocity,w->units))||!Finite(Gta(forward,1)))return RD_ERR_ARG;V v=Gta(velocity,w->units);float speed=sqrtf(v.x*v.x+v.y*v.y);
    V facing=Gta(forward,1);facing.z=0;float length=sqrtf(Dot(facing,facing));if(length>.001f)a->forward=facing*(1/length);
    a->target.enabled=1;a->target.force=1;V direction=speed>.01f?v*(1/speed):a->forward;
    // This actor is in a physical response. Do not ask a gait reference and
    // external root forces to propel it through the host's walk/run path.
    // Native balance steps and Shot remain free to transmit each impulse.
    // An unwounded contact actor keeps pursuing through native balance steps.
    // Shot actors retain the existing response and momentum rules.
    a->target.position[3]=((rd_world_capabilities(w)&2u)||a->pain==0.f)?speed:0;
    a->target.lean=0;
    // Do not have pursuit motors immediately cancel an incoming burst.
    // Momentum itself still enters the articulated solver, with no velocity
    // overwrite. Fire from behind does not brake forward pursuit.
    float opposing=(std::max)(0.f,-Dot(a->shotPressure,direction));
    float drive=(std::max)(0.f,1.f-opposing/.65f);
    a->target.reachPosition[3]=drive;
    if(a->target.lean>0)a->target.lean*=drive;
    V p=Position(w->Pose(actor&31,0))+direction*10;a->target.position[0]=p.x;a->target.position[1]=p.y;a->target.position[2]=p.z;return RD_OK;
}
API int32_t rd_actor_set_gait_reference(RdWorld* w,uint32_t actor,const RdQuat* rotations,float weight,unsigned stance){
    auto* a=w?w->Find(actor):nullptr;
    if(!a||!rotations||!std::isfinite(weight)||weight<0||weight>1||stance>3)return RD_ERR_ARG;
    nm108::GaitReference gait={};
    for(unsigned i=0;i<7;++i){Q q=Gta(rotations[i]);if(!Finite(q))return RD_ERR_ARG;
        const float components[]={q.x,q.y,q.z,q.w};
        for(unsigned j=0;j<4;++j)gait.rotation[i][j]=static_cast<int16_t>(components[j]*32767.f);}
    gait.weight=static_cast<uint16_t>(weight*65535.f);gait.stance=static_cast<uint16_t>(stance);
    a->gait=gait;a->gaitUntil=ClockMs()+150;return RD_OK;
}
API int32_t rd_actor_set_reach(RdWorld* w,uint32_t actor,float reach){auto* a=w?w->Find(actor):nullptr;if(!a||w->failed||!std::isfinite(reach))return RD_ERR_ARG;a->target.reach=(std::max)(0.f,(std::min)(1.f,reach));return RD_OK;}
API int32_t rd_world_update(RdWorld* w,float dt){
    if(!w||w->failed||!std::isfinite(dt)||dt<0||dt>.5f)return RD_ERR_ARG;w->accumulator+=dt;
    // Never owe the solver more than 100 ms: a slow stretch otherwise asks
    // for 8 steps per exchange, which take longer still (spiral of death).
    // The dropped time only slows the bodies during that stretch.
    w->accumulator=(std::min)(w->accumulator,6.f/60.f);
    if(w->client.Pending()){nm108::Response response={};int result=w->client.Poll(response);
        if(result<0||result>0&&response.status!=static_cast<unsigned>(nm108::Status::Complete)){w->failed=true;return RD_ERR_LOAD;}
        if(!result)return 0;w->Completed(w->pendingSteps);w->pendingSteps=0;}
    if(!w->DrainLifecycle())return RD_ERR_LOAD;
    if(w->birthPause){w->birthPause=false;return 0;}
    // Keep low-latency exchanges normally, but catch up at the same fixed
    // 60 Hz after a busy rendered frame. A permanent two-step cap makes the
    // physical clock fall behind on stairs/crowds when rendering drops <30 Hz.
    unsigned budget=w->asynchronous?(w->accumulator>.05f?8u:2u):8u;
    unsigned steps=(std::min)(budget,static_cast<unsigned>(w->accumulator*60.f));if(!steps)return 0;
    nm108::Request advance={};advance.command=static_cast<unsigned>(nm108::Command::Advance);
    for(unsigned id=0;id<nm108::MaxActors;++id){const auto& a=w->actors[id];
        if(a.allocated&&a.created&&a.mode==RD_MODE_ACTIVE&&(a.pain==0||a.animationReady)&&a.gaitUntil>=ClockMs()&&!(a.detachedMask&0x7e))
            advance.advance.gait[id]=a.gait;}
    int first=-1;for(unsigned id=0;id<nm108::MaxActors;++id)if(w->actors[id].allocated&&w->actors[id].created){if(first<0)first=id;auto& a=w->actors[id];if(a.mode==RD_MODE_ACTIVE){V point=Position(w->Pose(id,10))+a.forward*.85f;point.z+=.05f;a.target.reachPosition[0]=point.x;a.target.reachPosition[1]=point.y;a.target.reachPosition[2]=point.z;advance.advance.targets[id]=a.target;advance.advance.targetMask|=1u<<id;}}
    if(first<0){for(unsigned handle:w->fragments)if(handle){first=0;break;}if(first<0){w->accumulator=0;return 0;}}
    // Lossless batching only: divergent pellets retain their individual
    // points/directions, and a full blast never saturates at one hit's cap.
    if(w->hits.size()>1){std::vector<nm108::Hit> merged;merged.reserve(w->hits.size());
        for(const auto& h:w->hits){bool done=false;
            for(auto& m:merged)if(nm108::MergeHit(m,h)){done=true;break;}
            if(!done)merged.push_back(h);}
        w->hits.swap(merged);}
    // Ordinarily one exchange per game frame, independent of the crowd size.
    // Keep extra bullet batches separate so no burst event is dropped.
    unsigned remaining=steps;
    while(remaining){advance.actor=first;advance.steps=w->hits.size()>nm108::MaxHits?1:remaining;
        unsigned count=(std::min)(nm108::MaxHits,static_cast<unsigned>(w->hits.size()));advance.hitCount=count;for(unsigned h=0;h<count;++h)advance.hits[h]=w->hits[h];
        if(w->asynchronous){if(!w->client.Start(advance)){w->failed=true;return RD_ERR_LOAD;}w->pendingSteps=advance.steps;}
        else {if(!w->Submit(advance))return RD_ERR_LOAD;w->Completed(advance.steps);}
        w->hits.erase(w->hits.begin(),w->hits.begin()+count);w->accumulator-=advance.steps/60.f;remaining-=advance.steps;
        if(w->asynchronous)break;
    }
    return steps-remaining;
}
API int32_t rd_actor_get_state(const RdWorld* w,uint32_t actor,RdActorState* out){
    auto* a=w?w->Find(actor):nullptr;if(!a||!out||!a->created||w->failed)return RD_ERR_ARG;const auto& state=w->client.Actors()[actor&31];*out={};out->mode=a->mode;out->get_up_stage=-1;out->pain=a->pain;
    V com={state.centerOfMass[0],state.centerOfMass[1],state.centerOfMass[2]},velocity={state.velocity[0],state.velocity[1],state.velocity[2]};out->com=Host(com,w->units);out->com_velocity=Host(velocity,w->units);
    float feet=(state.poses[3][14]+state.poses[6][14])*.5f,height=state.poses[12][14]-feet;
    V trunk=Position(state.poses[10])-Position(state.poses[0]);
    float trunkLength=sqrtf(Dot(trunk,trunk));
    float upright=trunkLength>1e-5f?trunk.z/trunkLength:0.f;
    out->tilt=acosf((std::max)(-1.f,(std::min)(1.f,upright)));
    const bool standing=height>1.1f&&upright>.65f;
    out->flags=standing?RD_STATE_UPRIGHT|RD_STATE_NM_POSTURE:RD_STATE_DOWN;
    if(a->mode==RD_MODE_ACTIVE&&standing&&Dot(velocity,velocity)>.02f)out->flags|=RD_STATE_STEPPING;
    if(state.recoveryActive){out->flags=RD_STATE_DOWN|RD_STATE_GETTING_UP|RD_STATE_RECOVERY_MOTION|RD_STATE_NM_POSTURE;
        out->get_up_stage=RD_GETUP_RISE;}
    if(state.supportMask&nm108::BodySupport)out->flags|=262144u;
    if(state.supportMask&nm108::LoadedBody)out->flags|=524288u;
    // GTA's host resumes ordinary locomotion only once the physical response
    // has settled. Every bullet resets this eligibility, including mid-rise.
    if(a->mode==RD_MODE_ACTIVE&&a->animationReady){out->flags|=RD_STATE_BALANCED;if(state.detachedMask&((1u<<2)|(1u<<5)))out->flags&=~RD_STATE_DOWN;}
    return RD_OK;
}
API int32_t rd_actor_can_activate(const RdWorld* w,uint32_t actor){
    auto* pending=w?w->Find(actor):nullptr;if(!pending||pending->created)return RD_ERR_ARG;
    // The original RAGE collider needs its first real solver update before
    // another articulation is registered. Stagger same-frame host births;
    // BO1 continues its ordinary animation during this short deferral.
    // Delirium (capability 1) builds an articulation in well under a
    // millisecond and solves every body on its own: none of the original
    // runtime's staggering applies. With it a grenade's whole horde is born in
    // the blast's frame; staggered, the last of 20 waited 1.2 s, the crowd's
    // physics stood still for ~20 frames and late hits expired
    // (nm108_grenade_frame_verify.cpp).
    const bool delirium=(rd_world_capabilities(w)&1u)!=0;
    if(!delirium&&w->lastBirthFrame==w->client.State()->frame)return 0;
    RdVec3 lo[16],hi[16];if(rd_actor_get_bounds(w,actor,lo,hi))return RD_ERR_ARG;
    BirthClearance::RecoveryShape candidate[16];
    for(unsigned p=0;p<16;++p)candidate[p]=BirthClearance::RecoveryGeometry(w->Pose(actor&31,parts16[p]),w->rig.shapes[parts16[p]]);
    for(const auto& other:w->actors){if(!other.allocated||!other.created)continue;
        RdVec3 otherLo[16],otherHi[16];if(rd_actor_get_bounds(w,other.handle,otherLo,otherHi))return RD_ERR_LOAD;
        BirthClearance::RecoveryShape neighbour[16];bool haveShape[16]={};
        for(unsigned a=0;a<16;++a)for(unsigned b=0;b<16;++b){
            if((pending->detachedMask&(1u<<a))||(other.detachedMask&(1u<<b)))continue;
            // Bounds are only a cheap broad phase. Diagonal arms/legs have
            // large empty corners: rejecting those falsely delayed shots.
            // Real penetrating limbs/torso still veto the activation.
            if(lo[a].x<otherHi[b].x-1&&hi[a].x>otherLo[b].x+1&&
               lo[a].y<otherHi[b].y-1&&hi[a].y>otherLo[b].y+1&&
               lo[a].z<otherHi[b].z-1&&hi[a].z>otherLo[b].z+1){
                if(!haveShape[b]){neighbour[b]=BirthClearance::RecoveryGeometry(w->Pose(other.handle&31,parts16[b]),w->rig.shapes[parts16[b]]);haveShape[b]=true;}
                // Delirium meets other bodies as moving capsules and separates
                // a crowd's overlap softly; BO1's packed hordes overlap by more
                // than an inch all the time and were refused frame after frame.
                if(BirthClearance::ShapePenetration(candidate[a],neighbour[b])>(delirium?.12f:1.f/w->units))return 0;
            }
        }
    }
    // Do not pause the entire crowd for a candidate that still intersects a
    // window neighbour. A clear candidate asks for one idle lifecycle slot;
    // its overlap checks are repeated against the completed snapshot then.
    if(!delirium&&(w->client.Pending()||!w->lifecycle.empty())){
        const_cast<RdWorld*>(w)->birthPause=true;return 0;
    }
    return 1;
}
