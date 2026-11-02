// mod: euphoria - the Rust engine glue, see cg_euphoria_rd.h. NOT part of the original game.
#include "cg_euphoria_rd.h"
#include <euphoria/brush_faces.h>
#include "cg_local_coop.h"
#include "cg_local_mp.h"
#include <euphoria/euphoria_retarget.h>
#include <xanim/dobj.h>
#include <xanim/dobj_utils.h>
#include <xanim/dobj_skel.h>
#include <xanim/xmodel.h>
#include <euphoria/euphoria_bullet_impulse.h>
#include <euphoria/euphoria_crowd.h>
#include <clientscript/cscr_main.h>
#include <clientscript/cscr_stringlist.h>
#include <clientscript/cscr_vm.h>
#include <universal/q_shared.h>
#include <universal/com_math.h>
#include <universal/com_math_anglevectors.h>
#include <universal/dvar.h>
#include <qcommon/common.h>
#include <qcommon/cm_load.h>
#include <qcommon/cm_trace.h>
#include <physics/phys_traverse.h>
#include <physics/physpreset_load_obj.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <windows.h>

using namespace euphoria;

// --- the DLL's C interface (ragdoll\include\ragdoll.h), loaded at run time --------------------------------------
namespace {

struct RdVec3 { float x, y, z; };
struct RdQuat { float x, y, z, w; };
struct RdTransform { RdVec3 position; RdQuat rotation; };
struct RdHit { unsigned int body; RdVec3 point; RdVec3 impulse; };
struct RdWorldDesc { float units_per_meter; unsigned int axes; };
struct RdActorState { unsigned int mode, flags; int get_up_stage; unsigned int steps; RdVec3 com, com_velocity; float tilt, pain; };
struct RdWorld;
struct RdFragmentDesc {RdTransform transform;RdVec3 dimensions,linear,angular;float mass;};

const unsigned int RD_AXES_Z_UP = 1, RD_MODE_PASSIVE = 0, RD_MODE_ACTIVE = 1;
const int RD_STATE_BALANCED = 32;
// Keep the established floor mask, but import only geometry reachable from
// the world's BSP root. Inline model coordinates are not static world shapes.
const int RD_WORLD_CONTENTS = 0x2820011;

struct RdApi
{
    unsigned int (__cdecl *world_capabilities)(const RdWorld*);
    int (__cdecl *actor_push)(RdWorld*,unsigned int,unsigned int,RdVec3,float,unsigned int);
    int (__cdecl *model_body_masses)(const RdWorld *,float *);
    int (__cdecl *fragment_hit)(RdWorld *,unsigned int,RdVec3,RdVec3,float);
    int (__cdecl *actor_set_detached_parts)(RdWorld *,unsigned int,unsigned int);
    int (__cdecl *actor_set_gravity)(RdWorld *,unsigned int,float);
    int (__cdecl *actor_get_body_velocities)(const RdWorld *,unsigned int,RdVec3 *,RdVec3 *);
    int (__cdecl *fragment_spawn)(RdWorld *,const RdFragmentDesc *,unsigned int *);
    int (__cdecl *fragment_despawn)(RdWorld *,unsigned int);
    int (__cdecl *fragment_get_transform)(const RdWorld *,unsigned int,RdTransform *);
    int (__cdecl *world_set_async)(RdWorld *, unsigned int);
    RdWorld *(__cdecl *world_create_ex)(const RdWorldDesc *);
    void (__cdecl *world_destroy)(RdWorld *);
    int (__cdecl *world_set_params_ron)(RdWorld *, const char *);
    int (__cdecl *load_art)(RdWorld *, const char *, unsigned int *);
    int (__cdecl *model_rest_bodies)(const RdWorld *, unsigned int, RdTransform *);
    int (__cdecl *model_rest_segments)(const RdWorld *, unsigned int, RdVec3 *, RdVec3 *);
    int (__cdecl *add_static_mesh)(RdWorld *, const float *, unsigned int, const unsigned int *, unsigned int);
    int (__cdecl *add_static_convex)(RdWorld *, const float *, unsigned int);
    int (__cdecl *actor_spawn)(RdWorld *, unsigned int, const RdTransform *, RdVec3, unsigned int *);
    int (__cdecl *actor_despawn)(RdWorld *, unsigned int);
    int (__cdecl *actor_set_mode)(RdWorld *, unsigned int, unsigned int);
    int (__cdecl *actor_set_pose_rotations)(RdWorld *, unsigned int, const RdTransform *, const RdQuat *, RdVec3);
    int (__cdecl *actor_set_bodies)(RdWorld *, unsigned int, const RdTransform *, const RdVec3 *, const RdVec3 *);
    int (__cdecl *actor_get_box_vertices)(const RdWorld *, unsigned int, unsigned int, RdVec3 *);
    int (__cdecl *actor_hit)(RdWorld *, unsigned int, const RdHit *);
    int (__cdecl *actor_hit_profile)(RdWorld *,unsigned int,const RdHit *,float,float,unsigned int);
    int (__cdecl *weapon_response)(unsigned int,float*,float*,float*);
    int (__cdecl *actor_set_motion)(RdWorld *, unsigned int, RdVec3, RdVec3);
    int (__cdecl *actor_set_gait_reference)(RdWorld *,unsigned int,const RdQuat *,float,unsigned int);
    int (__cdecl *actor_set_reach)(RdWorld *, unsigned int, float);
    int (__cdecl *world_update)(RdWorld *, float);
    int (__cdecl *actor_get_bodies)(const RdWorld *, unsigned int, RdTransform *, unsigned int);
    int (__cdecl *actor_get_render_bodies)(const RdWorld *,unsigned int,RdTransform *,unsigned int);
    int (__cdecl *actor_get_state)(const RdWorld *, unsigned int, RdActorState *);
    int (__cdecl *actor_get_joint_positions)(const RdWorld *, unsigned int, RdVec3 *);
    int (__cdecl *actor_get_bounds)(const RdWorld *, unsigned int, RdVec3 *, RdVec3 *);
    int (__cdecl *actor_can_activate)(const RdWorld *, unsigned int);
};

RdApi g_api;
HMODULE g_dll;
bool g_dllTried;
RdWorld *g_world;
unsigned int g_model;
unsigned int g_mapChecksum;
const char *g_mapName;
bool g_mapFailed;                       // the world could not be made for this map: not retried until the next one
int g_lastStep;
bool g_haveRest;
Quat g_offBody[CG_RD_BODIES];           // body frame -> anatomical frame (from the engine's rest pose)
struct GibCapture {int time,ent,piece;Vec3 position,linear,angular;Quat rotation;float mass;bool pending;};
GibCapture g_gibCapture[128];unsigned g_gibCaptureNext;
struct GibBody {unsigned short id;unsigned int handle;Vec3 center;int born;};
GibBody g_gibBodies[128];
// Accessed under the existing Euphoria lock. A model entity can be rejected
// directly without scanning every debris body on each rendered frame.
unsigned char g_gibIndex[65536];
void ReindexGib(unsigned short id){g_gibIndex[id]=0;
    // Keep the original first-slot lookup if an entity ID is reused before
    // removal. Lifecycle work is rare; per-frame pose queries stay constant.
    for(unsigned i=0;i<128;++i)if(g_gibBodies[i].handle&&g_gibBodies[i].id==id){g_gibIndex[id]=static_cast<unsigned char>(i+1);break;}
}
GibBody* FindGib(unsigned short id){unsigned slot=g_gibIndex[id];
    if(!slot)return NULL;GibBody* body=&g_gibBodies[slot-1];return body->handle&&body->id==id?body:NULL;
}

const dvar_s *bo1_mod_euphoria_rd;
const dvar_s *bo1_mod_euphoria_rd_dll;
const dvar_s *bo1_mod_euphoria_rd_art;
const dvar_s *bo1_mod_euphoria_rd_params;
const dvar_s *bo1_mod_euphoria_rd_debug;

// the 16 bodies' bones (game names, '|' = alternatives) and the bone at the far end ("" = along the bone's own x)
struct RdBoneDesc { const char *bone; const char *tip; bool arm; };
const RdBoneDesc kRdBones[CG_RD_BODIES] =
{
    { "pelvis|j_mainroot", "j_spinelower", false },        // pelvis
    { "j_hip_le", "j_knee_le", false },                     // thigh_l
    { "j_knee_le", "j_ankle_le", false },                   // shin_l
    { "j_ankle_le", "j_ball_le", false },                   // foot_l
    { "j_hip_ri", "j_knee_ri", false },                     // thigh_r
    { "j_knee_ri", "j_ankle_ri", false },                   // shin_r
    { "j_ankle_ri", "j_ball_ri", false },                   // foot_r
    { "j_spinelower", "j_spine4|j_spineupper", false },     // spine_lower
    { "j_spine4|j_spineupper", "j_neck", false },           // chest
    { "j_neck", "j_head", false },                          // head
    { "j_shoulder_le", "j_elbow_le", true },                // upper_arm_l
    { "j_elbow_le", "j_wrist_le", true },                   // forearm_l
    { "j_wrist_le", "j_mid_le_1|j_index_le_1", true },       // hand_l: geometric palm axis, not mirrored bone +X
    { "j_shoulder_ri", "j_elbow_ri", true },                // upper_arm_r
    { "j_elbow_ri", "j_wrist_ri", true },                   // forearm_r
    { "j_wrist_ri", "j_mid_ri_1|j_index_ri_1", true },       // hand_r
};

template <typename T> bool Proc(T &f, const char *name)
{
    f = (T)GetProcAddress(g_dll, name);
    if ( !f )
        Com_Printf(18, "euphoria rd: %s missing from the DLL\n", name);
    return f != NULL;
}

bool LoadDll()
{
    if ( g_dll )
        return true;
    if ( g_dllTried )
        return false;
    g_dllTried = true;
    g_dll = LoadLibraryA(bo1_mod_euphoria_rd_dll->current.string);
    if ( !g_dll )
    {
        Com_Printf(18, "euphoria rd: cannot load %s (error %lu); the zombies keep the retail ragdoll\n", bo1_mod_euphoria_rd_dll->current.string, GetLastError());
        return false;
    }
    bool ok = Proc(g_api.world_create_ex, "rd_world_create_ex") && Proc(g_api.world_destroy, "rd_world_destroy")
        && Proc(g_api.world_set_params_ron, "rd_world_set_params_ron") && Proc(g_api.load_art, "rd_load_art")
        && Proc(g_api.model_rest_bodies, "rd_model_rest_bodies") && Proc(g_api.model_rest_segments, "rd_model_rest_segments")
        && Proc(g_api.add_static_mesh, "rd_add_static_mesh") && Proc(g_api.add_static_convex, "rd_add_static_convex")
        && Proc(g_api.actor_spawn, "rd_actor_spawn") && Proc(g_api.actor_despawn, "rd_actor_despawn")
        && Proc(g_api.actor_set_mode, "rd_actor_set_mode") && Proc(g_api.actor_set_pose_rotations, "rd_actor_set_pose_rotations")
        && Proc(g_api.actor_set_bodies, "rd_actor_set_bodies")
        && Proc(g_api.actor_get_box_vertices, "rd_actor_get_box_vertices")
        && Proc(g_api.actor_hit, "rd_actor_hit") && Proc(g_api.actor_set_motion, "rd_actor_set_motion") && Proc(g_api.actor_set_reach, "rd_actor_set_reach") && Proc(g_api.world_update, "rd_world_update")
        && Proc(g_api.actor_get_bodies, "rd_actor_get_bodies") && Proc(g_api.actor_get_state, "rd_actor_get_state")
        && Proc(g_api.actor_get_joint_positions, "rd_actor_get_joint_positions");
    if ( !ok )
    {
        FreeLibrary(g_dll);
        g_dll = NULL;
        return false;
    }
    g_api.actor_hit_profile=(decltype(g_api.actor_hit_profile))GetProcAddress(g_dll,"rd_actor_hit_profile");
    g_api.world_capabilities=(decltype(g_api.world_capabilities))GetProcAddress(g_dll,"rd_world_capabilities");
    g_api.actor_push=(decltype(g_api.actor_push))GetProcAddress(g_dll,"rd_actor_push");
    g_api.actor_set_gait_reference=(decltype(g_api.actor_set_gait_reference))GetProcAddress(g_dll,"rd_actor_set_gait_reference");
    g_api.actor_set_detached_parts=(decltype(g_api.actor_set_detached_parts))GetProcAddress(g_dll,"rd_actor_set_detached_parts");
    g_api.actor_set_gravity=(decltype(g_api.actor_set_gravity))GetProcAddress(g_dll,"rd_actor_set_gravity");
    g_api.actor_get_body_velocities=(decltype(g_api.actor_get_body_velocities))GetProcAddress(g_dll,"rd_actor_get_body_velocities");
    g_api.fragment_spawn=(decltype(g_api.fragment_spawn))GetProcAddress(g_dll,"rd_fragment_spawn");
    g_api.fragment_despawn=(decltype(g_api.fragment_despawn))GetProcAddress(g_dll,"rd_fragment_despawn");
    g_api.fragment_get_transform=(decltype(g_api.fragment_get_transform))GetProcAddress(g_dll,"rd_fragment_get_transform");
    g_api.fragment_hit=(decltype(g_api.fragment_hit))GetProcAddress(g_dll,"rd_fragment_hit");
    g_api.model_body_masses=(decltype(g_api.model_body_masses))GetProcAddress(g_dll,"rd_model_body_masses");
    g_api.weapon_response=(decltype(g_api.weapon_response))GetProcAddress(g_dll,"rd_weapon_response");
    g_api.actor_get_render_bodies=(decltype(g_api.actor_get_render_bodies))GetProcAddress(g_dll,"rd_actor_get_render_bodies");
    Com_Printf(18, "euphoria rd: %s loaded\n", bo1_mod_euphoria_rd_dll->current.string);
    return true;
}

Vec3 V(const RdVec3 &a) { return v3(a.x, a.y, a.z); }
RdVec3 R(Vec3 a) { RdVec3 r = { a.x, a.y, a.z }; return r; }
Quat Q(const RdQuat &a) { return q4(a.x, a.y, a.z, a.w); }
RdQuat RQ(Quat a) { RdQuat r = { a.x, a.y, a.z, a.w }; return r; }

// the anatomical frame of a segment: x along it, y towards the reference (the body's left for the trunk and legs, its
// front for the arms, which may point left in a T-pose bind), z = x * y. The same definition on the game's bind pose
// and on the engine's rest pose makes the two skeletons comparable whatever pose each was built in.
Quat AnatomicalFrame(Vec3 along, bool arm)
{
    Vec3 x = normalize(along);
    Vec3 refs[3] = { arm ? v3(1, 0, 0) : v3(0, 1, 0), arm ? v3(0, 1, 0) : v3(1, 0, 0), v3(0, 0, 1) };
    for ( Vec3 ref : refs )
    {
        Vec3 y = ref - x * dot(ref, x);
        if ( lengthSq(y) > 0.04f )
        {
            y = normalize(y);
            return qnormalize(qFromAxes(x, y, cross(x, y)));
        }
    }
    return qIdentity();
}

// Follow the same world leaves/material mask as BO1's collision queries.
// The global arrays also contain non-solid surfaces and local-space submodels
// (doors, removable debris). Importing all of them creates invisible obstacles.
void MarkWorldBrushes(int index, std::vector<unsigned char> &seen, std::vector<unsigned char> &brushes)
{
    if ( index <= 0 || (unsigned)index >= cm.leafbrushNodesCount || seen[index] ) return;
    seen[index] = 1;
    const cLeafBrushNode_s &n = cm.leafbrushNodes[index];
    if ( !(n.contents & RD_WORLD_CONTENTS) ) return;
    if ( n.leafBrushCount > 0 )
    {
        for ( int i = 0; i < n.leafBrushCount; ++i )
            if ( n.data.leaf.brushes[i] < cm.numBrushes ) brushes[n.data.leaf.brushes[i]] = 1;
        return;
    }
    if ( n.leafBrushCount < 0 ) MarkWorldBrushes(index + 1, seen, brushes);
    for ( int i = 0; i < 2; ++i )
        MarkWorldBrushes(index + n.data.children.childOffset[i], seen, brushes);
}

void MarkWorldTriangles(int index, std::vector<unsigned char> &seen, std::vector<unsigned char> &triangles)
{
    if ( index < 0 || index >= cm.aabbTreeCount || seen[index] ) return;
    seen[index] = 1;
    const CollisionAabbTree &n = cm.aabbTrees[index];
    if ( n.childCount )
    {
        for ( int i = 0; i < n.childCount; ++i ) MarkWorldTriangles(n.u.firstChildIndex + i, seen, triangles);
    }
    else if ( n.u.partitionIndex >= 0 && n.u.partitionIndex < cm.partitionCount )
    {
        const CollisionPartition &p = cm.partitions[n.u.partitionIndex];
        for ( int i = p.firstTri; i < p.firstTri + p.triCount && i < cm.triCount; ++i )
            if ( i >= 0 ) triangles[i] = 1;
    }
}

// World-space solid brushes and terrain. Moving submodels require a separate
// entity lifecycle/transform bridge; do not freeze them into the static map.
bool UploadMap()
{
    std::vector<unsigned char> brushMask(cm.numBrushes), brushSeen(cm.leafbrushNodesCount);
    std::vector<unsigned char> triMask(cm.triCount), treeSeen(cm.aabbTreeCount);
    // World traces start at BSP node zero. The global leaf array can also
    // contain leaves for inline models, whose geometry is in local space.
    std::vector<unsigned char> reached(cm.numNodes), worldLeaf(cm.numLeafs);
    std::vector<int> pending;if(cm.numNodes)pending.push_back(0);
    while(!pending.empty()){
        int node=pending.back();pending.pop_back();
        if(node<0){int leaf=-1-node;if((unsigned)leaf<cm.numLeafs)worldLeaf[leaf]=1;continue;}
        if((unsigned)node>=cm.numNodes||reached[node])continue;reached[node]=1;
        pending.push_back(cm.nodes[node].children[0]);pending.push_back(cm.nodes[node].children[1]);
    }
    for ( unsigned int i = 0; i < cm.numLeafs; ++i )
    {
        if(!worldLeaf[i])continue;
        const cLeaf_s &leaf = cm.leafs[i];
        MarkWorldBrushes(leaf.leafBrushNode, brushSeen, brushMask);
        for ( int t = leaf.firstCollAabbIndex; t < leaf.firstCollAabbIndex + leaf.collAabbCount && t < cm.aabbTreeCount; ++t )
            if ( cm.aabbTrees[t].materialIndex < cm.numMaterials && (cm.materials[cm.aabbTrees[t].materialIndex].contentFlags & RD_WORLD_CONTENTS) )
                MarkWorldTriangles(t, treeSeen, triMask);
    }
    int brushes = 0, failed = 0;
    for ( unsigned int i = 0; i < cm.numBrushes; ++i )
    {
        const cbrush_t &b = cm.brushes[i];
        if ( !brushMask[i] || !(b.contents & RD_WORLD_CONTENTS) || b.numverts < 4 || !b.verts )
            continue;
        if ( g_api.add_static_convex(g_world, &b.verts[0][0], b.numverts) == 0 )
            ++brushes;
        else {
            std::vector<BrushPlane> planes;
            for(unsigned axis=0;axis<3;++axis){Vec3 n=v3(0,0,0);(&n.x)[axis]=-1;planes.push_back({n,-b.mins[axis]});(&n.x)[axis]=1;planes.push_back({n,b.maxs[axis]});}
            for(unsigned side=0;side<b.numsides;++side){const auto* p=b.sides[side].plane;planes.push_back({v3(p->normal[0],p->normal[1],p->normal[2]),p->dist});}
            std::vector<unsigned> faces;
            bool recovered=BrushFaceTriangles(&b.verts[0][0],b.numverts,planes.data(),unsigned(planes.size()),faces);
            if(!recovered)recovered=BrushPlanarTriangles(&b.verts[0][0],b.numverts,faces);
            if(recovered
                &&g_api.add_static_mesh(g_world,&b.verts[0][0],b.numverts,faces.data(),unsigned(faces.size()/3))==0){
                ++brushes;Com_Printf(18,"euphoria rd: brush %u recovered from original boundary (%u vertices, %u triangles)\n",i,b.numverts,unsigned(faces.size()/3));
            }else{++failed;Com_Printf(18,"euphoria rd: brush %u could not be imported (%u vertices)\n",i,b.numverts);
                if(b.numverts<=8)for(unsigned p=0;p<b.numverts;++p)Com_Printf(18,"brush point %.9g %.9g %.9g\n",b.verts[p][0],b.verts[p][1],b.verts[p][2]);}
        }
    }
    int tris = 0;
    bool meshFailed = false;
    if ( cm.triCount > 0 && cm.verts && cm.triIndices && cm.vertCount > 0 )
    {
        std::vector<unsigned int> idx;
        for ( int i = 0; i < cm.triCount; ++i )
            if ( triMask[i] )
            {
                // BO1 uses cross(v0-v2,v0-v1) for the outward triangle
                // normal (CM_TracePointThroughTriangle). RAGE uses the
                // opposite winding. Unchanged indices turn floors into
                // downward contact planes and expel a landing body below them.
                idx.push_back(cm.triIndices[i*3]);
                idx.push_back(cm.triIndices[i*3+2]);
                idx.push_back(cm.triIndices[i*3+1]);
            }
        if ( !idx.empty() )
        {
            if ( g_api.add_static_mesh(g_world, &cm.verts[0][0], cm.vertCount, idx.data(), (unsigned int)idx.size() / 3) == 0 )
                tris = (int)idx.size() / 3;
            else
                meshFailed = true;
        }
    }
    Com_Printf(18, "euphoria rd: map %s: %i brushes (%i not convex), %i triangles\n", cm.name ? cm.name : "?", brushes, failed, tris);
    return failed == 0 && !meshFailed;
}

void DestroyWorld()
{
    if ( g_world )
        g_api.world_destroy(g_world);
    g_world = NULL;
    memset(g_gibCapture,0,sizeof(g_gibCapture));memset(g_gibBodies,0,sizeof(g_gibBodies));memset(g_gibIndex,0,sizeof(g_gibIndex));g_gibCaptureNext=0;
    g_haveRest = false;
}

bool CreateWorld()
{
    RdWorldDesc desc = { 39.37f, RD_AXES_Z_UP };
    g_world = g_api.world_create_ex(&desc);
    if ( !g_world )
        return false;
    if(g_api.world_capabilities && (g_api.world_capabilities(g_world)&1u))
        Com_Printf(18,"euphoria rd: Delirium Rust backend, original 21-part articulation and full message interface\n");
    if ( g_api.load_art(g_world, bo1_mod_euphoria_rd_art->current.string, &g_model) != 0 )
    {
        Com_Printf(18, "euphoria rd: cannot load the body %s\n", bo1_mod_euphoria_rd_art->current.string);
        DestroyWorld();
        return false;
    }
    // tuning: the testbed's params.ron, if there is one
    FILE *f = fopen(bo1_mod_euphoria_rd_params->current.string, "rb");
    if ( f )
    {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if ( n > 0 && n < (1 << 20) )
        {
            char *text = new char[n + 1];
            text[fread(text, 1, n, f)] = 0;
            int r = g_api.world_set_params_ron(g_world, text);
            Com_Printf(18, "euphoria rd: params %s %s\n", bo1_mod_euphoria_rd_params->current.string, r == 0 ? "loaded" : "not valid, defaults kept");
            delete[] text;
        }
        fclose(f);
    }
    // the engine's rest pose, as anatomical frames
    RdTransform rest[CG_RD_BODIES];
    RdVec3 s[CG_RD_BODIES], e[CG_RD_BODIES];
    if ( g_api.model_rest_bodies(g_world, g_model, rest) != 0 || g_api.model_rest_segments(g_world, g_model, s, e) != 0 )
    {
        DestroyWorld();
        return false;
    }
    for ( int i = 0; i < CG_RD_BODIES; ++i )
    {
        // A farthest cuboid corner is not a toe axis: it rolled/pitched the
        // soles by about 10 degrees. Both rigs have planted feet in rest pose.
        Quat frame = i == 3 || i == 6 ? qIdentity() : AnatomicalFrame(V(e[i]) - V(s[i]), kRdBones[i].arm);
        g_offBody[i] = qnormalize(conj(Q(rest[i].rotation)) * frame);
    }
    g_haveRest = true;
    if ( !UploadMap() )
    {
        Com_Printf(18, "euphoria rd: map collision upload failed; physical takeover disabled\n");
        DestroyWorld();
        return false;
    }
    // Optional extension: older Rust libraries retain their existing ABI.
    g_api.actor_get_bounds = (decltype(g_api.actor_get_bounds))GetProcAddress(g_dll,"rd_actor_get_bounds");
    g_api.actor_can_activate = (decltype(g_api.actor_can_activate))GetProcAddress(g_dll,"rd_actor_can_activate");
    g_api.world_set_async = (decltype(g_api.world_set_async))GetProcAddress(g_dll,"rd_world_set_async");
    if(g_api.world_set_async&&g_api.world_set_async(g_world,1)!=0){
        Com_Printf(18,"euphoria rd: native collision initialization failed\n");
        DestroyWorld();return false;
    }
    // One-off collision/BVH upload is native loading time, not a script loop.
    // Keep the normal watchdog active for subsequent script execution.
    Scr_ResetTimeout(SCRIPTINSTANCE_CLIENT);
    return true;
}

bool LookupBone(const DObj *obj, const char *names, unsigned char *outIdx)
{
    char name[48];
    const char *p = names;
    while ( *p )
    {
        int n = 0;
        while ( p[n] && p[n] != '|' && n < 47 )
            ++n;
        memcpy(name, p, n);
        name[n] = 0;
        p += n;
        if ( *p == '|' )
            ++p;
        unsigned int str = SL_FindString(name, SCRIPTINSTANCE_SERVER);
        if ( str )
        {
            unsigned char idx = 254;
            if ( DObjGetBoneIndex(obj, str, &idx, -1) )
            {
                *outIdx = idx;
                return true;
            }
        }
    }
    return false;
}

Vec3 MatPos(const DObjAnimMat &m) { return v3(m.trans[0], m.trans[1], m.trans[2]); }
Quat MatQuat(const DObjAnimMat &m) { return qnormalize(q4(m.quat[0], m.quat[1], m.quat[2], m.quat[3])); }

// the nearest body to a point: its segment from this joint to the next
int NearestBody(const Vec3 *joints, Vec3 p)
{
    static const int next[CG_RD_BODIES] = { 7, 2, 3, -1, 5, 6, -1, 8, 9, -1, 11, 12, -1, 14, 15, -1 };
    int best = 8;
    float bestD = 1e30f;
    for ( int i = 0; i < CG_RD_BODIES; ++i )
    {
        Vec3 a = joints[i];
        Vec3 b = next[i] >= 0 ? joints[next[i]] : a;
        Vec3 ab = b - a;
        float t = clampf(dot(p - a, ab) / (lengthSq(ab) + 1e-6f), 0.0f, 1.0f);
        float d = lengthSq(p - (a + ab * t));
        if ( d < bestD )
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

} // namespace

void CG_EuphoriaRd_RegisterDvars()
{
    bo1_mod_euphoria_rd = _Dvar_RegisterBool("bo1_mod_euphoria_rd", false, 0, "mod: euphoria - a fallen zombie's body is the Rust engine's (ragdoll_ffi.dll): it falls against the map, gets up by itself and hands back to the animation (0 = BO1's ragdoll)");
    bo1_mod_euphoria_rd_dll = _Dvar_RegisterString("bo1_mod_euphoria_rd_dll", "ragdoll_ffi.dll", 0, "mod: euphoria - path of the Rust engine's 32-bit DLL");
    bo1_mod_euphoria_rd_art = _Dvar_RegisterString("bo1_mod_euphoria_rd_art", "character.xrt", 0, "mod: euphoria - the body proportions the Rust engine reads (your local Xbox 360 SDK export)");
    bo1_mod_euphoria_rd_params = _Dvar_RegisterString("bo1_mod_euphoria_rd_params", "ragdoll_params.ron", 0, "mod: euphoria - the Rust engine's tuning file (the testbed's params.ron), if present");
    bo1_mod_euphoria_rd_debug = _Dvar_RegisterBool("bo1_mod_euphoria_rd_debug", false, 0, "mod: euphoria - print the Rust bodies' hand-overs and states");
}

bool CG_EuphoriaRd_On()
{
    if ( !bo1_mod_euphoria_rd || !bo1_mod_euphoria_rd->current.enabled )
        return false;
    if ( !LoadDll() )
        return false;
    // a new map: a new world (the old one's statics are another level's)
    bool sameMap = cm.checksum == g_mapChecksum && cm.name == g_mapName;
    if ( !sameMap )
    {
        DestroyWorld();
        g_mapFailed = false;
    }
    if ( !g_world && !g_mapFailed && cm.isInUse )
    {
        g_mapChecksum = cm.checksum;
        g_mapName = cm.name;
        g_mapFailed = !CreateWorld();
    }
    return g_world != NULL && g_haveRest;
}
bool CG_EuphoriaRd_Debug(){return bo1_mod_euphoria_rd_debug&&bo1_mod_euphoria_rd_debug->current.enabled;}
bool CG_EuphoriaRd_Continuous(){return g_world&&g_api.world_capabilities&&(g_api.world_capabilities(g_world)&2u)!=0;}
void CG_EuphoriaRd_ResetMap(){
    DestroyWorld();g_mapChecksum=0;g_mapName=nullptr;g_mapFailed=false;g_lastStep=0;
}

void CG_EuphoriaRd_Frame(int now)
{
    if ( !g_world )
        return;
    if ( g_lastStep == 0 || now < g_lastStep || now - g_lastStep > 500 )
    {
        g_lastStep = now;
        return;
    }
    if ( now == g_lastStep )
        return;
    float dt = (float)(now - g_lastStep) * 0.001f;
    g_lastStep = now;
    if ( g_api.world_update(g_world, dt) < 0 )
    {
        Com_Printf(18, "euphoria rd: physical runtime update failed; physical takeover disabled\n");
        DestroyWorld();
        g_mapFailed = true;
    }
}

static unsigned int SkinIdentity(const DObj* obj)
{
    unsigned int hash=2166136261u;
    hash=(hash^(unsigned int)obj->numBones)*16777619u;
    hash=(hash^(unsigned int)obj->duplicateParts)*16777619u;
    const unsigned char* parents=(const unsigned char*)(obj->models+obj->numModels);
    for(int m=0;m<obj->numModels;++m){
        hash=(hash^(unsigned int)(uintptr_t)obj->models[m])*16777619u;
        hash=(hash^parents[m])*16777619u;
    }
    return hash;
}

bool CG_EuphoriaRd_Bind(cg_rd_state_t *rd, const DObj *obj)
{
    memset(rd, 0, sizeof(*rd));
    if ( !g_haveRest || !obj || obj->numModels < 1 || !obj->models || !obj->models[0] || !obj->models[0]->baseMat
        || obj->numBones > CG_RD_BONES )
        return false;
    rd->boneCount = obj->numBones;
    rd->skinIdentity=SkinIdentity(obj);
    memset(rd->parent, -1, sizeof(rd->parent));
    memset(rd->bodyForBone, -1, sizeof(rd->bodyForBone));
    // BO1 heads and accessories can be separate XModels, with duplicate bones.
    const unsigned char *modelParents = (const unsigned char *)(obj->models + obj->numModels);
    int offset = 0;
    for ( int m = 0; m < obj->numModels; ++m )
    {
        const XModel *model = obj->models[m];
        for ( int j = 0; j < model->numBones; ++j )
            rd->parent[offset + j] = j < model->numRootBones
                ? (modelParents[m] == 255 ? -1 : modelParents[m])
                : offset + j - model->parentList[j - model->numRootBones];
        offset += model->numBones;
    }
    if ( obj->duplicateParts )
    {
        const unsigned char *dup = (const unsigned char *)SL_ConvertToString(obj->duplicateParts, SCRIPTINSTANCE_SERVER) + 20;
        for ( ; dup[0]; dup += 2 )
            rd->parent[dup[0] - 1] = dup[1] - 1;
    }
    alignas(16) DObjAnimMat rest[CG_RD_BONES];
    int all[5] = { -1, -1, -1, -1, -1 };
    DObjCalcBaseSkel(obj, rest, all);
    for ( int i = 0; i < CG_RD_BODIES; ++i )
    {
        rd->valid[i] = LookupBone(obj, kRdBones[i].bone, &rd->bone[i]) && rd->bone[i] < obj->numBones;
        if ( !rd->valid[i] ) {
            // A replacement damage model can omit a hand/head. Rebuild its
            // actual hierarchy rather than writing stale bone indices into it.
            static const int parentBody[16]={-1,0,1,2,0,4,5,0,7,8,8,10,11,8,13,14};
            if(parentBody[i]<0)return false;
            rd->bone[i]=rd->bone[parentBody[i]];
        }
        else rd->bodyForBone[rd->bone[i]] = (short)i;
    }
    // the bind pose (model space: x forward, y left, z up) as anatomical frames
    for ( int i = 0; i < CG_RD_BODIES; ++i )
    {
        const DObjAnimMat &m = rest[rd->bone[i]];
        Quat q = MatQuat(m);
        Vec3 along;
        unsigned char tip = 0;
        if ( kRdBones[i].tip[0] && LookupBone(obj, kRdBones[i].tip, &tip) && tip < obj->numBones )
            along = MatPos(rest[tip]) - MatPos(m);
        else
            along = rotate(q, v3(1, 0, 0));
        if ( lengthSq(along) < 1e-4f )
            along = rotate(q, v3(1, 0, 0));
        Quat frame = i == 3 || i == 6 ? qIdentity() : AnatomicalFrame(along, kRdBones[i].arm);
        rd->offBone[i] = qnormalize(conj(q) * frame);
    }
    for ( int i = 0; i < rd->boneCount; ++i )
    {
        int p = rd->parent[i];
        if ( p >= i )
            return false;
        rd->localOffset[i] = p >= 0 ? rotateInv(MatQuat(rest[p]), MatPos(rest[i])-MatPos(rest[p])) : v3(0,0,0);
        rd->driven[i] = rd->bodyForBone[i] >= 0 || (p >= 0 && rd->driven[p]);
    }
    if ( bo1_mod_euphoria_rd_debug->current.enabled )
        Com_Printf(18, "euphoria rd: bind %s: %i bones, %i models, pelvis %i, original hierarchy retained\n",
            obj->models[0]->name, rd->boneCount, obj->numModels, rd->bone[0]);
    rd->bound = true;
    return true;
}

bool CG_EuphoriaRd_EnsureSkin(cg_rd_state_t* rd,const DObj* obj)
{
    if(!obj||!obj->models||!obj->numModels)return false;
    if(rd->skinIdentity==SkinIdentity(obj))return true;
    cg_rd_state_t skin={};if(!CG_EuphoriaRd_Bind(&skin,obj))return false;
    // Replace only the skin binding. The articulation, wound history, death,
    // velocities and recovery continue in the same native runtime slot.
    memcpy(rd->bone,skin.bone,sizeof(rd->bone));memcpy(rd->valid,skin.valid,sizeof(rd->valid));
    memcpy(rd->offBone,skin.offBone,sizeof(rd->offBone));memcpy(rd->parent,skin.parent,sizeof(rd->parent));
    memcpy(rd->bodyForBone,skin.bodyForBone,sizeof(rd->bodyForBone));memcpy(rd->driven,skin.driven,sizeof(rd->driven));
    memcpy(rd->localOffset,skin.localOffset,sizeof(rd->localOffset));
    rd->boneCount=skin.boneCount;rd->skinIdentity=skin.skinIdentity;rd->serverSkinIdentity=0;rd->havePhysicalPose=false;
    // Model changes (including dismemberment) are not skeletal motion.
    // Start a new animation sample pair without disturbing live body velocity.
    rd->previousBoneTime=rd->sampledVelocityTime=0;
    return true;
}

bool CG_EuphoriaRd_SkinMatches(const cg_rd_state_t* rd,const DObj* obj)
{
    if(!obj||!obj->models||!obj->numModels||obj->numBones!=rd->boneCount)return false;
    unsigned int identity=SkinIdentity(obj);if(rd->serverSkinIdentity==identity)return true;
    // Server/client DObjs can have different attachment handles. Match the
    // actual named body bones, not process-local string/model identities.
    for(int i=0;i<CG_RD_BODIES;++i)if(rd->valid[i]){
        unsigned char bone=0;if(!LookupBone(obj,kRdBones[i].bone,&bone)||bone!=rd->bone[i])return false;
    }
    rd->serverSkinIdentity=identity;return true;
}

bool CG_EuphoriaRd_AnimBones(int boneCount, const DObj *obj, const float *origin, float yawDeg, cg_rd_pose_t *pose)
{
    if ( boneCount <= 0 || boneCount > CG_RD_BONES || obj->numBones != boneCount )
        return false;
    alignas(16) DObjAnimMat scratch[256];
    DObj tmp;
    memcpy(&tmp, obj, sizeof(tmp));
    tmp.locked = 0;
    tmp.skel.mat = scratch;
    memset(&tmp.skel.partBits, 0, sizeof(tmp.skel.partBits));
    int all[5] = { -1, -1, -1, -1, -1 };
    DObjCalcSkel(&tmp, all);
    float angles[3] = { 0.0f, yawDeg, 0.0f };
    float axis[3][3];
    AnglesToAxis(angles, axis);
    Quat qPose = qnormalize(qFromAxes(v3(axis[0][0], axis[0][1], axis[0][2]), v3(axis[1][0], axis[1][1], axis[1][2]), v3(axis[2][0], axis[2][1], axis[2][2])));
    for ( int i = 0; i < boneCount; ++i )
    {
        const DObjAnimMat &m = scratch[i];
        Vec3 t = MatPos(m);
        Quat mq = MatQuat(m);
        if ( !finite3(t) || !finiteq(mq) )
            return false;
        pose->pos[i] = v3(origin[0], origin[1], origin[2]) + rotate(qPose, t);
        pose->q[i] = qnormalize(qPose * mq);
    }
    return true;
}

bool CG_EuphoriaRd_Activate(cg_rd_state_t *rd, const Vec3 *animPos, const Quat *animQ, Vec3 vel, Vec3 fallDir, float fallImpulse, int now)
{
    if ( !g_world || !rd->bound )
        return false;
    rd->activationDeferred=false;
    RdTransform root = { R(animPos[0]), RQ(qIdentity()) };
    RdVec3 zero = { 0, 0, 0 };
    int spawn=g_api.actor_spawn(g_world,g_model,&root,zero,&rd->actor);
    if(spawn!=0)
    {
        // A corpse may still occupy the last slot when the next wave enters.
        // Retry after it is released instead of disabling this zombie forever.
        if(spawn!=-1){rd->activationDeferred=true;rd->activationRetryTime=now+150;}
        return false;
    }
    if(rd->detachedMask&&g_api.actor_set_detached_parts&&g_api.actor_set_detached_parts(g_world,rd->actor,rd->detachedMask)){
        g_api.actor_despawn(g_world,rd->actor);return false;
    }
    // every body's rotation from its bone's (through the anatomical frames); the pelvis body where the pelvis bone is
    RdQuat rots[CG_RD_BODIES];
    for ( int i = 0; i < CG_RD_BODIES; ++i )
        rots[i] = RQ(qnormalize(animQ[i] * rd->offBone[i] * conj(g_offBody[i])));
    RdTransform pelvis = { R(animPos[0]), rots[0] };
    if ( g_api.actor_set_pose_rotations(g_world, rd->actor, &pelvis, rots, R(vel)) != 0 )
    {
        g_api.actor_despawn(g_world, rd->actor);
        return false;
    }
    // the engine's legs are not the zombie's length: move the body so its ankles are where the animation's are
    RdVec3 joints[CG_RD_BODIES];
    if ( !(rd->detachedMask&((1u<<2)|(1u<<5)))&&g_api.actor_get_joint_positions(g_world, rd->actor, joints) == 0 )
    {
        Vec3 d = (animPos[3] + animPos[6]) * 0.5f - (V(joints[3]) + V(joints[6])) * 0.5f;
        if ( finite3(d) && lengthSq(d) < 40.0f * 40.0f )
        {
            pelvis.position = R(animPos[0] + d);
            if ( g_api.actor_set_pose_rotations(g_world, rd->actor, &pelvis, rots, R(vel)) != 0 )
            {
                g_api.actor_despawn(g_world, rd->actor);
                return false;
            }
        }
    }
    // An ankle is a joint anchor, not the bottom of the foot collision box.
    // Fitting only ankle bones can seed both soles inside a thin floor, where
    // the original one-sided mesh contacts expel them downward. Fit actual ART
    // soles once at hand-over; subsequent contact is entirely physical.
    float groundLift=0.0f;
    if ( g_api.actor_get_bounds )
    {
        RdVec3 mins[CG_RD_BODIES],maxs[CG_RD_BODIES];
        if ( g_api.actor_get_bounds(g_world,rd->actor,mins,maxs)==0 )
        {
            for ( int foot : {3,6} )
            {
                if(rd->detachedMask&(1u<<foot))continue;
                RdVec3 corners[8];
                if(g_api.actor_get_box_vertices(g_world,rd->actor,foot,corners)!=0)continue;
                for ( int sample=0; sample<8; ++sample )
                {
                    // Test actual rotated foot vertices. AABB corners belong
                    // to empty space and used to catch adjacent higher steps.
                    float x=corners[sample].x,y=corners[sample].y;
                    float start[3]={x,y,maxs[foot].z+0.25f},end[3]={x,y,corners[sample].z-32},zero[3]={};
                    trace_t trace={};col_context_t context;
                    CM_BoxTrace(&trace,start,end,zero,zero,RD_WORLD_CONTENTS,&context);
                    if ( !trace.startsolid && !trace.allsolid && trace.fraction<1 && trace.normal.vec.v[2]>.55f )
                    {
                        float ground=start[2]+(end[2]-start[2])*trace.fraction;
                        float lift=ground+0.25f-corners[sample].z;
                        if ( lift>groundLift && lift<16.0f ) groundLift=lift;
                    }
                }
            }
            if ( groundLift>0 )
            {
                pelvis.position.z+=groundLift;
                if ( g_api.actor_set_pose_rotations(g_world,rd->actor,&pelvis,rots,R(vel))!=0 )
                { g_api.actor_despawn(g_world,rd->actor);return false; }
            }
        }
    }
    // Do not register an articulated torso inside a window frame. BO1 keeps
    // completing its traversal until the physical volume fits the map.
    if ( g_api.actor_get_bounds )
    {
        RdVec3 mins[CG_RD_BODIES],maxs[CG_RD_BODIES];
        if ( g_api.actor_get_bounds(g_world,rd->actor,mins,maxs)==0 )
        {
            for ( int part : {0,7,8,9} )
            {
                if(rd->detachedMask&(1u<<part))continue;
                float center[3]={(mins[part].x+maxs[part].x)*.5f,(mins[part].y+maxs[part].y)*.5f,(mins[part].z+maxs[part].z)*.5f};
                float extents[3]={(maxs[part].x-mins[part].x)*.4f,(maxs[part].y-mins[part].y)*.4f,(maxs[part].z-mins[part].z)*.4f};
                float lo[3]={-extents[0],-extents[1],-extents[2]},hi[3]={extents[0],extents[1],extents[2]};
                trace_t trace={};col_context_t context;
                CM_BoxTrace(&trace,center,center,lo,hi,RD_WORLD_CONTENTS,&context);
                if ( trace.startsolid||trace.allsolid )
                {
                    g_api.actor_despawn(g_world,rd->actor);
                    rd->activationDeferred=true;rd->activationRetryTime=now+100;
                    if(bo1_mod_euphoria_rd_debug->current.enabled)
                        Com_Printf(18,"euphoria rd: defer torso %i at window %.2f %.2f %.2f\n",part,center[0],center[1],center[2]);
                    return false;
                }
            }
        }
    }
    if(g_api.actor_can_activate&&g_api.actor_can_activate(g_world,rd->actor)==0)
    {
        // This gate also requests one idle solver slot for a clear body.
        // Retry next client frame so that slot is used, rather than sleeping
        // 100 ms and repeatedly missing it. All overlap checks still apply.
        g_api.actor_despawn(g_world,rd->actor);rd->activationDeferred=true;rd->activationRetryTime=now+1;return false;
    }
    // the pelvis bone in the pelvis body's frame (drawn from the body from now on)
    RdTransform bodies[CG_RD_BODIES];
    if ( g_api.actor_get_bodies(g_world, rd->actor, bodies, CG_RD_BODIES) != 0 )
    {
        g_api.actor_despawn(g_world, rd->actor);
        return false;
    }
    int elapsed=now-rd->sampledVelocityTime;
    if ( rd->sampledVelocityTime && elapsed>=0 && elapsed<150 )
    {
        RdVec3 linear[CG_RD_BODIES], angular[CG_RD_BODIES];
        for ( int i=0; i<CG_RD_BODIES; ++i )
        {
            // The sampled point is a BO1 bone, not Fred's body centre.
            // Transport its velocity to that centre before handing it over.
            Vec3 omega=rd->sampledAngularVelocity[i];
            linear[i]=R(vel+rd->sampledLocalVelocity[i]+cross(omega,V(bodies[i].position)-animPos[i]));
            angular[i]=R(omega);
        }
        if ( g_api.actor_set_bodies(g_world,rd->actor,bodies,linear,angular) != 0 )
        {
            g_api.actor_despawn(g_world, rd->actor);
            return false;
        }
    }
    // Keep the skin on the raised collision rig; baking the correction into
    // the render offset would put the visible soles back through the floor.
    rd->pelvisOff = rotateInv(Q(bodies[0].rotation), animPos[0]+v3(0,0,groundLift)-V(bodies[0].position));
    if ( g_api.actor_set_mode(g_world, rd->actor, RD_MODE_ACTIVE) != 0 )
    {
        Com_Printf(18, "euphoria rd: body activation failed\n");
        g_api.actor_despawn(g_world, rd->actor);
        return false;
    }
    // the fall: a push at the chest along the way it goes
    if ( fallImpulse > 0.0f && lengthSq(fallDir) > 0.01f )
    {
        Vec3 d = normalize(v3(fallDir.x, fallDir.y, 0.0f));
        RdHit hit = { 8, R(V(bodies[8].position)), R(d * fallImpulse) };
        g_api.actor_hit(g_world, rd->actor, &hit);
    }
    rd->phase = 1;
    rd->havePhysicalPose = false;
    rd->locomotionReady=false;
    rd->animationReturn.reset();
    rd->blend = 0.0f;
    rd->startTime = now;
    rd->balancedSince = 0;
    rd->deadSince = 0;
    rd->reportFlags = rd->reportStage = -99;
    rd->reportSteps = 0;
    if ( bo1_mod_euphoria_rd_debug->current.enabled )
    {
        Com_Printf(18, "euphoria rd: body %u takes over at %.0f %.0f %.0f, vel %.0f %.0f %.0f, fall %.0f N*s\n", rd->actor, animPos[0].x, animPos[0].y, animPos[0].z, vel.x, vel.y, vel.z, fallImpulse);
        Com_Printf(18,"euphoria rd sole fit: body %u lift %.3f\n",rd->actor,groundLift);
        Com_Printf(18,"euphoria animation handover: body %u sample_age %i foot_spin %.3f %.3f\n",rd->actor,
            rd->sampledVelocityTime?now-rd->sampledVelocityTime:-1,
            length(rd->sampledAngularVelocity[3]),length(rd->sampledAngularVelocity[6]));
        for ( int mask : { 1, 0x2820011 } )
        {
            float start[3]={animPos[0].x,animPos[0].y,animPos[0].z+80};
            float end[3]={start[0],start[1],animPos[0].z-200},zero[3]={};
            trace_t trace={}; col_context_t context;
            CM_BoxTrace(&trace,start,end,zero,zero,mask,&context);
            Com_Printf(18,"euphoria rd landing: body %u mask %x floor %.3f fraction %.4f solid %i/%i contents %x native pelvis %.3f feet %.3f %.3f\n",
                rd->actor,mask,start[2]+(end[2]-start[2])*trace.fraction,trace.fraction,trace.startsolid,trace.allsolid,trace.cflags,
                bodies[0].position.z,bodies[3].position.z,bodies[6].position.z);
        }
    }
    return true;
}

void CG_EuphoriaRd_Write(const cg_rd_state_t *rd, const DObj *obj, int *partBits, const cg_rd_pose_t &anim)
{
    if ( !g_world || rd->phase == 0 || obj->numBones != rd->boneCount )
        return;
    // A DObj is calculated piecemeal for hit traces and drawing. Lifecycle
    // exchanges can finish physics halfway through that same client frame.
    // All its parts must use one complete pose, including already drawn roots.
    if ( !rd->havePhysicalPose || rd->physicalPoseTime != g_lastStep )
    {
    RdTransform bodies[CG_RD_BODIES];
    if ( (g_api.actor_get_render_bodies?g_api.actor_get_render_bodies:g_api.actor_get_bodies)(g_world, rd->actor, bodies, CG_RD_BODIES) != 0 )
        return;
    DObjAnimMat *skel = DObjGetRotTransArray(obj);
    if ( !skel )
        return;
    Quat physicalQ[CG_RD_BODIES];
    for ( int i = 0; i < CG_RD_BODIES; ++i )
        physicalQ[i] = qnormalize(Q(bodies[i].rotation) * g_offBody[i] * conj(rd->offBone[i]));
    Vec3 root = V(bodies[0].position) + rotate(Q(bodies[0].rotation), rd->pelvisOff);
    cg_rd_pose_t draw;
    retargetHierarchy(rd->boneCount, rd->parent, rd->bodyForBone, rd->bone[0],
        anim.pos, anim.q, physicalQ, root, rd->blend, draw.pos, draw.q, rd->localOffset);
    for ( int i = 0; i < rd->boneCount; ++i )
        if ( !finite3(draw.pos[i]) || !finiteq(draw.q[i]) ) return;
    rd->physicalPose = draw;
    rd->havePhysicalPose = true;
    rd->physicalPoseTime = g_lastStep;
    if ( bo1_mod_euphoria_rd_debug->current.enabled && rd->blend > 0.99f && g_lastStep - rd->reportGeometryTime > 1000 )
    {
        RdVec3 joints[CG_RD_BODIES];
        if ( g_api.actor_get_joint_positions(g_world, rd->actor, joints) == 0 )
        {
            Vec3 dl = draw.pos[rd->bone[3]] - V(joints[3]);
            Vec3 dr = draw.pos[rd->bone[6]] - V(joints[6]);
            Com_Printf(18, "euphoria rd geometry: body %u ankle delta L %.2f %.2f %.2f R %.2f %.2f %.2f pelvisOff %.2f %.2f %.2f time %i\n",
                rd->actor, dl.x, dl.y, dl.z, dr.x, dr.y, dr.z, rd->pelvisOff.x, rd->pelvisOff.y, rd->pelvisOff.z, g_lastStep);
            Com_Printf(18,"euphoria rd position: body %u pelvis %.3f %.3f %.3f feet %.3f %.3f time %i\n",
                rd->actor,bodies[0].position.x,bodies[0].position.y,bodies[0].position.z,bodies[3].position.z,bodies[6].position.z,g_lastStep);
        }
        rd->reportGeometryTime = g_lastStep;
    }
    }
    CG_EuphoriaRd_WriteCached(rd, obj, partBits);
}

bool CG_EuphoriaRd_WriteCached(const cg_rd_state_t *rd, const DObj *obj, int *partBits)
{
    if ( !g_world || !rd->phase || obj->numBones != rd->boneCount
        || !rd->havePhysicalPose || rd->physicalPoseTime != g_lastStep )
        return false;
    DObjAnimMat *skel = DObjGetRotTransArray(obj);
    if ( !skel ) return false;
    const cg_rd_pose_t &draw = rd->physicalPose;
    CG_LocalCoop_Publish(rd,obj);
    for ( int idx = 0; idx < rd->boneCount; ++idx )
    {
        if ( !rd->driven[idx] || (partBits[idx >> 5] & (0x80000000u >> (idx & 31))) == 0 )
            continue;
        Vec3 tr = draw.pos[idx];
        Quat q = draw.q[idx];
        if ( !finite3(tr) || !finiteq(q) )
            continue;
        if ( !DObjSetSkelRotTransIndex(obj, partBits, idx) )
            continue;
        DObjAnimMat *m = &skel[idx];
        m->trans[0] = tr.x;
        m->trans[1] = tr.y;
        m->trans[2] = tr.z;
        m->quat[0] = q.x;
        m->quat[1] = q.y;
        m->quat[2] = q.z;
        m->quat[3] = q.w;
        float l2 = qdot(q, q);
        m->transWeight = l2 > 0.0f ? 2.0f / l2 : 2.0f;
    }
    return true;
}

bool CG_EuphoriaRd_WeaponResponse(unsigned profile,float &impulse,float &duration,float &speed)
{
    return g_api.actor_hit_profile && g_api.weapon_response && g_api.weapon_response(profile,&impulse,&duration,&speed)==0;
}

void CG_EuphoriaRd_Hit(cg_rd_state_t *rd, Vec3 point, Vec3 dir, float J, int body,float duration,float speed,unsigned profile)
{
    if ( !g_world || rd->phase == 0 || J <= 0.0f )
        return;
    RdVec3 joints[CG_RD_BODIES];
    if ( g_api.actor_get_joint_positions(g_world, rd->actor, joints) != 0 )
        return;
    Vec3 j[CG_RD_BODIES];
    for ( int i = 0; i < CG_RD_BODIES; ++i )
        j[i] = V(joints[i]);
    if ( body < 0 || body >= CG_RD_BODIES ) body = NearestBody(j, point);
    // Native weapon pushes use their full momentum distributed in time.
    // Legacy one-frame pokes retain the old small-limb safety calibration.
    if(!profile)J=bulletLimbImpulse(J,body);
    // Render and collider proportions differ; do not turn that mismatch into
    // an arbitrarily long torque arm. The struck bone still selects the muscle.
    Vec3 offset = point - j[body];
    if ( lengthSq(offset) > 12.0f * 12.0f ) point = j[body] + normalize(offset) * 12.0f;
    RdHit hit = { (unsigned int)body, R(point), R(normalize(dir) * J) };
    int result=profile&&g_api.actor_hit_profile?g_api.actor_hit_profile(g_world,rd->actor,&hit,duration,speed,profile):g_api.actor_hit(g_world, rd->actor, &hit);
    if(!result){rd->phase=1;rd->locomotionReady=false;rd->balancedSince=0;rd->startTime=CG_GetLocalClientGlobals(0)->time;}
    if(result)Com_Printf(18,"euphoria rd: rejected bullet ent %u profile %u (%i)\n",rd->actor,profile,result);
    if ( bo1_mod_euphoria_rd_debug->current.enabled )
        Com_Printf(18, "euphoria rd: body %u hit on body %u, %.2f N*s duration %.3f profile %u\n", rd->actor, hit.body, J,duration,profile);
}

int CG_EuphoriaRd_Flags(const cg_rd_state_t *rd)
{
    if ( !g_world || rd->phase == 0 )
        return -1;
    RdActorState st;
    if ( g_api.actor_get_state(g_world, rd->actor, &st) != 0 )
        return -1;
    if ( bo1_mod_euphoria_rd_debug->current.enabled &&
        ((int)st.flags != rd->reportFlags || st.get_up_stage != rd->reportStage || st.steps != rd->reportSteps) )
    {
        Com_Printf(18, "euphoria rd: body %u flags %u getup %i steps %u tilt %.2f pain %.1f\n",
            rd->actor, st.flags, st.get_up_stage, st.steps, st.tilt, st.pain);
        rd->reportFlags = (int)st.flags;
        rd->reportStage = st.get_up_stage;
        rd->reportSteps = st.steps;
    }
    return (int)st.flags;
}

void CG_EuphoriaRd_SetDead(cg_rd_state_t *rd, int now)
{
    if ( rd->deadSince )
        return;
    if(!g_world||rd->phase==0){rd->deadSince=now?now:1;return;}
    g_api.actor_set_mode(g_world, rd->actor, RD_MODE_PASSIVE);
    // Death may arrive during the blend back to locomotion. Preserve the
    // current articulation; no death clip or animation can take over now.
    rd->phase=1;rd->blend=1.f;rd->locomotionReady=false;rd->balancedSince=0;
    rd->deadSince = now ? now : 1;
}

bool CG_EuphoriaRd_Root(const cg_rd_state_t *rd, float *out, float *yawDeg)
{
    if ( !g_world || rd->phase == 0 )
        return false;
    RdTransform bodies[CG_RD_BODIES];
    if ( g_api.actor_get_bodies(g_world, rd->actor, bodies, CG_RD_BODIES) != 0 )
        return false;
    Vec3 p = V(bodies[0].position);
    if ( !finite3(p) )
        return false;
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
    if ( yawDeg )
    {
        // facing: the pelvis body's anatomical frame, y = its left
        Quat f = qnormalize(Q(bodies[0].rotation) * g_offBody[0]);
        Vec3 left = rotate(f, v3(0, 1, 0));
        Vec3 fwd = cross(left, v3(0, 0, 1));
        *yawDeg = atan2f(fwd.y, fwd.x) * 57.29578f;
    }
    return true;
}

void CG_EuphoriaRd_Release(cg_rd_state_t *rd)
{
    CG_LocalCoop_Forget(rd);
    if ( g_world && rd->phase != 0 )
        g_api.actor_despawn(g_world, rd->actor);
    rd->phase = 0;
    rd->pendingContact = false;
    rd->blend = 0.0f;
    rd->havePhysicalPose = false;
    rd->previousBoneTime=0;
    rd->sampledVelocityTime=0;
    rd->lastAnimTime=0;
}

// Moon: zombie_moon_gravity.gsc marks a zombie in low gravity with actor client flag 0
// (level._ZOMBIE_ACTOR_FLAG_LOW_GRAVITY); its body then falls with the moon's 136/800.
void CG_EuphoriaRd_Gravity(cg_rd_state_t *rd, bool low)
{
    if (!g_world || rd->phase == 0 || !g_api.actor_set_gravity) return;
    g_api.actor_set_gravity(g_world, rd->actor, low ? 136.f / 800.f : 1.f);
}

void CG_EuphoriaRd_Motion(cg_rd_state_t *rd, int now)
{
    if (!g_world || rd->phase == 0) return;
    Vec3 v = !rd->deadSince && now - rd->motionTime < 250 ? rd->desiredVelocity : v3(0,0,0);
    RdVec3 velocity = {v.x, v.y, v.z};
    RdVec3 forward = {rd->desiredForward.x, rd->desiredForward.y, rd->desiredForward.z};
    g_api.actor_set_motion(g_world, rd->actor, velocity, forward);
    g_api.actor_set_reach(g_world, rd->actor, rd->deadSince ? 0.0f : rd->reach);
}

void CG_EuphoriaRd_GaitReference(cg_rd_state_t* rd,const cg_rd_pose_t& anim){
    if(!g_world||!rd->phase||rd->deadSince||!g_api.actor_set_gait_reference)return;
    RdQuat rotations[7];
    for(unsigned i=0;i<7;++i)
        rotations[i]=RQ(qnormalize(anim.q[rd->bone[i]]*rd->offBone[i]*conj(g_offBody[i])));
    float left=anim.pos[rd->bone[3]].z,right=anim.pos[rd->bone[6]].z;
    // Reference stance is checked against actual sole contacts by Delirium.
    unsigned stance=(left<=right+2.f?1u:0u)|(right<=left+2.f?2u:0u);
    // Begin with the supported return to locomotion. BO1 pauses its ordinary
    // locomotion cycle during a full response; that frozen pose must not drive
    // a falling body's legs. New hits revoke locomotionReady immediately.
    // BO1 owns ordinary running. During the return, blend the rendered bones;
    // a sprint pose must not also torque the recovering physical knees.
    g_api.actor_set_gait_reference(g_world,rd->actor,rotations,rd->locomotionReady&&CG_EuphoriaRd_Continuous()?.35f:0.f,stance);
}

void CG_EuphoriaRd_GibEvent(cg_rd_state_t* rd,int entnum,unsigned int parm,int now){
    if(!g_world||!g_api.actor_set_detached_parts||!g_api.fragment_spawn)return;
    // Retail tags refer to pieces cut at the elbows/knees, not the shoulder
    // or hip. Guts are an effect and never remove the load-bearing torso.
    unsigned tags=parm&255;bool all=(tags&1)!=0;unsigned old=rd->detachedMask;
    // Retail accepts only a head gib after the first non-head dismemberment.
    // Do not remove collisions for a second event whose model change is ignored.
    if(old&~(1u<<9)){tags&=1u<<5;all=false;}
    if(tags&(1u<<6))rd->detachedMask|=(1u<<10)|(1u<<11)|(1u<<12);
    const unsigned masks[5]={(1u<<14)|(1u<<15),(1u<<11)|(1u<<12),(1u<<5)|(1u<<6),(1u<<2)|(1u<<3),1u<<9};
    const unsigned body[4]={14,11,5,2};
    RdTransform poses[16]={};RdVec3 v[16]={},w[16]={};
    float masses[16]={};if(g_api.model_body_masses)g_api.model_body_masses(g_world,masses);
    bool native=rd->phase&&g_api.actor_get_bodies(g_world,rd->actor,poses,16)==0;
    if(native&&g_api.actor_get_body_velocities)g_api.actor_get_body_velocities(g_world,rd->actor,v,w);
    for(unsigned piece=0;piece<5;++piece)if(all||(tags&(1u<<(piece+1)))){
        rd->detachedMask|=masks[piece];if(piece==4||(old&masks[piece]))continue;
        unsigned p=body[piece];auto& c=g_gibCapture[g_gibCaptureNext++%128];c={};c.time=now;c.ent=entnum;c.piece=piece;c.pending=true;
        c.mass=masses[p]+masses[p+1];if(c.mass<.05f)c.mass=2.f;
        c.position=rd->previousBonePos[p];c.rotation=rd->previousBoneQ[p];c.linear=rd->animationVelocity;
        if(native){
            c.rotation=qnormalize(Q(poses[p].rotation)*g_offBody[p]*conj(rd->offBone[p]));
            c.position=rd->havePhysicalPose?rd->physicalPose.pos[rd->bone[p]]:V(poses[p].position);
            c.angular=V(w[p]);c.linear=V(v[p])+cross(c.angular,c.position-V(poses[p].position));
        }
        float speed=length(c.linear);if(speed>472.44f)c.linear=c.linear*(472.44f/speed);
        float spin=length(c.angular);if(spin>18.849556f)c.angular=c.angular*(18.849556f/spin);
    }
    if(native&&old!=rd->detachedMask){
        if(g_api.actor_set_detached_parts(g_world,rd->actor,rd->detachedMask))Com_Printf(18,"euphoria rd: dismemberment rejected ent %i mask %x\n",entnum,rd->detachedMask);
        rd->havePhysicalPose=false;rd->balancedSince=0;rd->locomotionReady=false;rd->phase=1;
    }
}

bool CG_EuphoriaRd_PrepareGib(int now,const char* model,float* pos,float* quat,Vec3* linear,Vec3* angular,float* mass){
    if(!g_world||!g_api.fragment_spawn)return false;
    if(!model||(!strstr(model,"armspawn")&&!strstr(model,"legspawn")))return false;
    Vec3 original=v3(pos[0],pos[1],pos[2]);GibCapture* best=NULL;float distance=32.f*32.f;
    int piece=strstr(model,"rarmspawn")?0:strstr(model,"larmspawn")?1:strstr(model,"rlegspawn")?2:strstr(model,"llegspawn")?3:-1;
    if(piece<0)return false;
    for(auto& c:g_gibCapture)if(c.pending&&c.piece==piece){
        if(now<c.time||now-c.time>500){c.pending=false;continue;}
        float d=lengthSq(c.position-original);if(d<distance){distance=d;best=&c;}
    }
    if(!best)return false;best->pending=false;
    pos[0]=best->position.x;pos[1]=best->position.y;pos[2]=best->position.z;
    quat[0]=best->rotation.x;quat[1]=best->rotation.y;quat[2]=best->rotation.z;quat[3]=best->rotation.w;
    *linear=best->linear;*angular=best->angular;*mass=best->mass;return true;
}

bool CG_EuphoriaRd_BindGib(unsigned short id,const XModel* model,const float* pos,const float* quat,Vec3 linear,Vec3 angular,float mass,unsigned short* expiredId){
    if(expiredId)*expiredId=0xffff;
    if(!g_world||!g_api.fragment_spawn||!model)return false;
    GibBody* slot=NULL;for(auto& b:g_gibBodies)if(!b.handle){slot=&b;break;}
    if(!slot){
        // Bound debris cost in long rounds and retire its original visual too.
        // Never leave an orphan that can wake a second physics engine.
        slot=&g_gibBodies[0];for(auto& b:g_gibBodies)if(b.born<slot->born)slot=&b;
        if(g_api.fragment_despawn(g_world,slot->handle))return false;
        if(expiredId)*expiredId=slot->id;slot->handle=0;ReindexGib(slot->id);
    }
    float lo[3],hi[3];XModelGetBounds(model,lo,hi);
    Vec3 center=v3((lo[0]+hi[0])*.5f,(lo[1]+hi[1])*.5f,(lo[2]+hi[2])*.5f);
    Quat rotation=qnormalize(q4(quat[0],quat[1],quat[2],quat[3]));Vec3 offset=rotate(rotation,center);
    Vec3 position=v3(pos[0],pos[1],pos[2])+offset;linear+=cross(angular,offset);
    RdFragmentDesc desc={};desc.transform={R(position),RQ(rotation)};
    desc.dimensions={clampf(hi[0]-lo[0],1.f,78.f),clampf(hi[1]-lo[1],1.f,78.f),clampf(hi[2]-lo[2],1.f,78.f)};
    float speed=length(linear);if(speed>472.44f)linear=linear*(472.44f/speed);
    desc.linear=R(linear);desc.angular=R(angular);desc.mass=clampf(mass,.1f,20.f);
    unsigned handle=0;if(g_api.fragment_spawn(g_world,&desc,&handle))return false;
    *slot={id,handle,center,CG_GetLocalClientGlobals(0)->time};ReindexGib(id);
    Com_Printf(18,"euphoria gib: id %u native %u model %s velocity %.2f spin %.2f mass %.2f\n",id,handle,model->name,length(linear),length(angular),desc.mass);
    return true;
}

bool CG_EuphoriaRd_GibPose(unsigned short id,int now,float* pos,float* quat){
    if(!g_world||!g_api.fragment_get_transform)return false;
    if(GibBody* body=FindGib(id)){auto& b=*body;
        RdTransform transform;if(g_api.fragment_get_transform(g_world,b.handle,&transform))return false;
        Quat q=Q(transform.rotation);Vec3 p=V(transform.position)-rotate(q,b.center);
        pos[0]=p.x;pos[1]=p.y;pos[2]=p.z;quat[0]=q.x;quat[1]=q.y;quat[2]=q.z;quat[3]=q.w;return true;
    }return false;
}
bool CG_EuphoriaRd_IsGib(unsigned short id){
    return FindGib(id)!=NULL;
}
void CG_EuphoriaRd_RemoveGib(unsigned short id){
    if(!FindGib(id))return;
    for(auto& body:g_gibBodies)if(body.handle&&body.id==id){if(g_world&&g_api.fragment_despawn)g_api.fragment_despawn(g_world,body.handle);body={};}
    g_gibIndex[id]=0;
}
bool CG_EuphoriaRd_GibHit(unsigned short id,Vec3 point,Vec3 impulse,float duration){
    if(!g_world||!g_api.fragment_hit)return false;
    if(const GibBody* body=FindGib(id)){g_api.fragment_hit(g_world,body->handle,R(point),R(impulse),duration);return true;}
    return false;
}

void CG_EuphoriaRd_Explosion(cg_rd_state_t* rd,Vec3 deltaVelocity){
    if(!g_world||!rd->phase||!g_api.actor_push||!g_api.model_body_masses)return;
    float masses[16];if(g_api.model_body_masses(g_world,masses))return;
    for(unsigned i=0;i<16;++i){if(rd->detachedMask&(1u<<i))continue;
        Vec3 impulse=deltaVelocity*masses[i];float n=length(impulse);if(n>127.f)impulse=impulse*(127.f/n);
        g_api.actor_push(g_world,rd->actor,i,R(impulse),.08f,9);
    }
    rd->phase=1;rd->locomotionReady=false;rd->balancedSince=0;
}
bool CG_EuphoriaRd_CorpseSettled(cg_rd_state_t* rd,int now){
    if(!g_world||!rd->phase||!g_api.actor_get_body_velocities)return false;
    RdVec3 linear[16],angular[16];if(g_api.actor_get_body_velocities(g_world,rd->actor,linear,angular))return false;
    for(unsigned i=0;i<16;++i)if(!(rd->detachedMask&(1u<<i))&&(lengthSq(V(linear[i]))>7.874f*7.874f||lengthSq(V(angular[i]))>.35f*.35f)){
        rd->corpseSettledSince=0;return false;
    }
    if(!rd->corpseSettledSince)rd->corpseSettledSince=now?now:1;
    return now-rd->corpseSettledSince>750;
}
void CG_EuphoriaRd_AnimatedPush(cg_rd_state_t* rd,Vec3 origin,Vec3 velocity,float radius,float dt){
    if(!g_world||!rd->phase||!g_api.actor_push||!g_api.actor_get_bounds)return;
    RdVec3 lo[16],hi[16];if(g_api.actor_get_bounds(g_world,rd->actor,lo,hi))return;
    unsigned closest=16;float best=radius*radius;Vec3 point={};
    for(unsigned i=0;i<16;++i){if(rd->detachedMask&(1u<<i))continue;
        Vec3 low=V(lo[i]),high=V(hi[i]);if(high.z<origin.z||low.z>origin.z+70.f)continue;
        Vec3 q=v3(clampf(origin.x,low.x,high.x),clampf(origin.y,low.y,high.y),clampf(origin.z+35.f,low.z,high.z));
        float d=(q.x-origin.x)*(q.x-origin.x)+(q.y-origin.y)*(q.y-origin.y);
        if(d<best){best=d;closest=i;point=q;}
    }
    if(closest==16)return;
    Vec3 normal=point-origin;normal.z=0;if(lengthSq(normal)<.1f)normal=velocity;normal=normalize(normal);
    RdActorState state={};if(g_api.actor_get_state(g_world,rd->actor,&state))return;
    float closing=dot(velocity-V(state.com_velocity),normal);if(closing<=5.f)return;
    // A moving animated actor is a prescribed driver: only the articulated
    // neighbour receives momentum, never an opposing shove on the runner.
    float force=clampf(closing/39.37f*60.f,0.f,240.f);
    Vec3 impulse=normal*(force*clampf(dt,0.f,.1f));float n=length(impulse);if(n>12.f)impulse=impulse*(12.f/n);
    int now=CG_GetLocalClientGlobals(0)->time;
    if(rd->animatedPushTime!=now){rd->animatedPushTime=now;rd->animatedPushBudget=240.f*clampf(dt,0.f,.1f);}
    n=length(impulse);if(n>rd->animatedPushBudget&&n>0)impulse=impulse*(rd->animatedPushBudget/n);
    rd->animatedPushBudget=(std::max)(0.f,rd->animatedPushBudget-length(impulse));
    if(lengthSq(impulse)<.0001f)return;
    g_api.actor_push(g_world,rd->actor,closest,R(impulse),.05f,8);
}

bool CG_EuphoriaRd_CrowdBounds(const cg_rd_state_t* rd,Vec3* lo,Vec3* hi,Vec3* velocity){
    if(!g_world||!rd->phase||!g_api.actor_get_bounds)return false;
    RdVec3 lower[16],upper[16];RdActorState state={};
    if(g_api.actor_get_bounds(g_world,rd->actor,lower,upper)||g_api.actor_get_state(g_world,rd->actor,&state))return false;
    for(unsigned i=0;i<16;++i){lo[i]=V(lower[i]);hi[i]=V(upper[i]);}
    *velocity=V(state.com_velocity);return true;
}
bool CG_EuphoriaRd_CrowdMotion(const cg_rd_state_t* rd,CrowdMotion* out){
    if(!g_world||!rd||!rd->phase||!out||!g_api.actor_get_body_velocities||!g_api.model_body_masses)return false;
    RdTransform bodies[16];RdVec3 linear[16],angular[16];
    if(g_api.actor_get_bodies(g_world,rd->actor,bodies,16)
        ||g_api.actor_get_body_velocities(g_world,rd->actor,linear,angular)
        ||g_api.model_body_masses(g_world,out->mass))return false;
    out->totalMass=0;bool sampledMotion=false;
    for(unsigned i=0;i<16;++i){
        out->center[i]=V(bodies[i].position);out->linear[i]=V(linear[i]);out->angular[i]=V(angular[i]);
        if(!finite3(out->center[i])||!finite3(out->linear[i])||!finite3(out->angular[i])
            ||!std::isfinite(out->mass[i])||out->mass[i]<=0)return false;
        if(!(rd->detachedMask&(1u<<i)))out->totalMass+=out->mass[i];
        sampledMotion|=lengthSq(out->linear[i])+lengthSq(out->angular[i])>0;
    }
    // The first native snapshot has no previous pose for the bridge's
    // finite-difference velocities. Preserve its known COM translation until
    // the second sample, rather than classifying a newly launched body as idle.
    if(!sampledMotion){
        RdActorState state={};
        if(g_api.actor_get_state(g_world,rd->actor,&state)||!finite3(V(state.com_velocity)))return false;
        for(unsigned i=0;i<16;++i)out->linear[i]=V(state.com_velocity);
    }
    return out->totalMass>0;
}
bool CG_EuphoriaRd_CrowdImpulse(cg_rd_state_t* rd,Vec3 impulse,float dt,int now){
    if(!g_world||!rd->phase||!g_api.actor_push)return false;
    // Total budget belongs to the receiving body, not to each runner in a pile.
    if(rd->animatedPushTime!=now){rd->animatedPushTime=now;rd->animatedPushBudget=240.f*clampf(dt,0.f,.1f);}
    float n=length(impulse);if(n>rd->animatedPushBudget&&n>0)impulse=impulse*(rd->animatedPushBudget/n);
    if(lengthSq(impulse)<.0001f)return false;
    // Pelvis contact transmits through the existing joints without injecting
    // a whole-body impulse into a hand or an already weakened detached limb.
    if(g_api.actor_push(g_world,rd->actor,0,R(impulse),clampf(dt,.01f,.1f),8))return false;
    rd->animatedPushBudget=(std::max)(0.f,rd->animatedPushBudget-length(impulse));return true;
}
