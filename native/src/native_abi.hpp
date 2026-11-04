#pragma once
#include <cstdint>
#include <cstddef>
#include "rig_hierarchy.hpp"
namespace native {
template<class T> T& at(void* p,size_t offset){return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset);}
template<class T> const T& at(const void* p,size_t offset){return *reinterpret_cast<const T*>(static_cast<const unsigned char*>(p)+offset);}
struct Mat {float quat[4],trans[3],transWeight;};
struct Model {
    const char* name;uint8_t numBones,numRootBones,numSurfs,lodRamp;
    uint16_t* boneNames;uint8_t* parents;int16_t* quats;float* trans;
    uint8_t* classification;Mat* base;
};
struct Skeleton {uint32_t anim[5],control[5],skel[5];int timestamp;Mat* mat;};
struct DObj {
    void* tree;uint16_t duplicates,entnum;uint8_t duplicateBytes,numModels,numBones,pad;
    uint32_t ignoreCollision;volatile long locked;Skeleton skel;float radius;
    uint32_t hide[5];uint8_t localClient,flags;uint16_t pad2;void* ikState;Model** models;
};
static_assert(sizeof(Mat)==32&&sizeof(DObj)==0x7c);
static_assert(offsetof(DObj,skel.mat)==0x54&&offsetof(DObj,models)==0x78);
struct Plane {float normal[3],dist;uint8_t type,signbits,pad[2];};
struct Side {Plane* plane;int contents,surface;};
struct alignas(8) Brush {float mins[3];int contents;float maxs[3];unsigned sidesCount;Side* sides;int axialContents[2][3],axialSurface[2][3];unsigned vertexCount;float(*vertices)[3];};
struct Node {Plane* plane;int16_t children[2];};
struct Leaf {uint16_t firstAabb,aabbCount;int brushContents,terrainContents;float mins[3],maxs[3];int brushNode;int16_t cluster;uint16_t pad;};
struct BrushNode {uint8_t axis,pad;int16_t brushCount;int contents;union {uint16_t* brushes;struct {float distance,range;uint16_t child[2];} children;} data;};
struct Aabb {float origin[3];uint16_t material,childCount;float halfSize[3];int firstChildOrPartition;};
struct Partition {uint8_t triangleCount,borderCount;uint16_t pad;int firstTriangle,nuinds,fuind;void* borders;};
struct Material {char name[64];int surface,contents;};
struct ClipMap {
    const char* name;int inUse,planeCount;Plane* planes;unsigned modelCount;void* models;
    unsigned materialCount;Material* materials;unsigned sideCount;Side* sides;
    unsigned nodeCount;Node* nodes;unsigned leafCount;Leaf* leafs;
    unsigned brushNodeCount;BrushNode* brushNodes;unsigned leafBrushCount;uint16_t* leafBrushes;
    unsigned leafSurfaceCount;unsigned* leafSurfaces;unsigned vertexCount;float(*vertices)[3];
    unsigned brushVertexCount;float(*brushVertices)[3];unsigned uindCount;uint16_t* uinds;
    int triangleCount;uint16_t* triangleIndices;uint8_t* walkable;
    int borderCount;void* borders;int partitionCount;Partition* partitions;
    int aabbCount;Aabb* aabbs;unsigned submodelCount;void* submodels;
    uint16_t brushCount,pad;Brush* brushes;
};
static_assert(sizeof(Brush)==96&&sizeof(Leaf)==44&&sizeof(BrushNode)==20);
static_assert(offsetof(ClipMap,triangleCount)==104&&offsetof(ClipMap,brushes)==152);
inline void* entity(int index){return reinterpret_cast<void*>(0x1A796F8+index*0x34c);}
inline bool actor(void* ent){return at<uint16_t>(ent,0xbe)==0x10&&at<void*>(ent,0x140)!=nullptr;}
inline bool humanoidActor(void* ent){
    return actor(ent)&&rig::humanoid(at<int>(at<void*>(ent,0x140),8));
}
// BO1 SP Actor_DoMove 00575313..00575350 selects these gravity dvars.
// Repeat that selection rather than reading actor+0xE30: new and scripted
// actors can retain zero there until their first native movement update.
inline float actorGravityScale(void* ent){
    const auto* normal=*reinterpret_cast<void* const*>(0xBCAEFC);
    const auto* zombies=*reinterpret_cast<void* const*>(0x243FDD4);
    const auto* arcade=*reinterpret_cast<void* const*>(0x247FDE8);
    const bool moon=at<bool>(zombies,0x18)&&!at<bool>(arcade,0x18)&&
        at<uint16_t>(ent,0x160)==*reinterpret_cast<const uint16_t*>(0x23A5884);
    const auto* selected=moon?*reinterpret_cast<void* const*>(0xBCD0B8):normal;
    return at<float>(selected,0x18)/at<float>(normal,0x18);
}
inline int time(){return *reinterpret_cast<int*>(0x1c04890);}
inline DObj* serverObject(int entity){return reinterpret_cast<DObj*(__cdecl*)(int)>(0x47e340)(entity);}
inline DObj* clientObject(int entity){return reinterpret_cast<DObj*(__cdecl*)(int,int)>(0x501850)(entity,0);}
inline bool isClientObject(const DObj* obj){return obj->entnum&&obj->entnum<=1024&&clientObject(obj->entnum-1)==obj;}
inline float* rootRate(DObj* obj,uint16_t& info){
    if(!obj||!obj->tree)return nullptr;
    info=at<uint16_t>(obj->tree,4);if(!info||info>=4096)return nullptr;
    void* node=reinterpret_cast<void*>(0x2770fd8+info*64);
    if(at<uint16_t>(node,16)!=0)return nullptr;
    return &at<float>(node,0x30);
}
inline const char* string(uint16_t id){return reinterpret_cast<const char*(__cdecl*)(unsigned,int)>(0x687530)(id,0);}
struct Trace {float normal[4],fraction;int surface,contents,type;uint16_t id,model,partName,bone,group;bool allSolid,startSolid,walkable;void* staticModel;int partition;};
static_assert(sizeof(Trace)==0x38);
inline Trace trace(const float* start,const float* end,const float* mins,const float* maxs,int entity){
    Trace result{};int context[10]={0,0,0,0,1023,1023,0,0,0,0};
    reinterpret_cast<void(__cdecl*)(Trace*,const float*,const float*,const float*,const float*,int,int,void*)>(0x68fd90)(
        &result,start,mins,maxs,end,entity,0x2820011,context);return result;
}
}
