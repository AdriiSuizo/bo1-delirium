#pragma once
// Numeric host/runtime exchange only. Contains no original executable or asset.
// The retail x86 runtime owns its own process and TLS; the BO1 host never maps
// GTA's image into its address space. Events establish request/response order.
#include <cstdint>
#include <cstddef>
namespace nm108 {
constexpr uint32_t Magic=0x4e4d3138,Version=18,PartCount=21,MaxActors=32,MaxHits=32,MaxVertices=128,MaxTriangles=256,MaxFragments=128;
// Optional footprint evidence in the existing support mask.
constexpr uint32_t SolePolygon=1u<<24;
constexpr uint32_t SoleCapture=1u<<25;
constexpr uint32_t SoleCaptureBlend=1u<<26;
constexpr uint32_t BodySupport=1u<<30;
constexpr uint32_t LoadedBody=1u<<29;
constexpr uint32_t BodyContact=1u<<28;
constexpr uint32_t FixtureFloor=1;
// MeleeImpulse: a knife/bayonet shove, GTA IV's melee push (NMBalance + lean away).
constexpr unsigned ContactImpulse=8,ExplosionImpulse=9,MeleeImpulse=10;
enum class Command:uint32_t {Advance=1,Close=2,AddMesh=3,SetInitialPose=4,CreateActor=5,RemoveActor=6,ReadActor=7,SetTarget=8,SetMode=9,SetDetachedParts=10,CreateFragment=11,RemoveFragment=12,FragmentImpulse=13,Message=14,SetGravity=15};
enum class Status:uint32_t {Starting=0,Ready=1,Complete=2,InvalidRequest=3,Nonfinite=4,ControllerFailure=5,Closed=6};
struct Hit {
    uint32_t part,actor;
    // point.w=0: world point (direct runtime probes); 1: limb-local hit,
    // reconstructed against the current complete physics pose by the worker.
    // impulse.w: duration in seconds (zero = instantaneous); normal.w:
    // weapon-response kind. xyz retain their usual vector meanings.
    float point[4],impulse[4],bulletVelocity[4],normal[4];
};
struct Mesh {
    uint32_t vertices,triangles;
    float points[MaxVertices][4];
    uint16_t indices[MaxTriangles][3];
    float transform[16];
};
struct InitialPose {float pose[PartCount][16],linearVelocity[PartCount][4],angularVelocity[PartCount][4];};
// position.w: locomotion speed in m/s; reachPosition.w: available propulsion
// after directional wounds. Original message handlers receive xyz only.
struct Target {float position[4],lean;uint32_t force,enabled;float reachPosition[4],reach;};
// One host frame carries all pursuit targets and its queued bullets. The hits
// occupy the same union offset as the original request.hits API for probes.
// BO1 references drive joint motors only. Signed normalized quaternions keep
// the entire batched exchange below 64 KiB without adding per-actor IPC calls.
struct GaitReference {int16_t rotation[7][4];uint16_t weight,stance;};
struct Advance {Hit hits[MaxHits];Target targets[MaxActors];uint32_t targetMask;GaitReference gait[MaxActors];};
struct Shape {float orientation[4],offset[4],size[4],length,radius;uint32_t box;float mass;};
struct Rig {int32_t parent[PartCount];float anchor[PartCount][4];Shape shapes[PartCount];};
struct Fragment {float pose[16],dimensions[4],linear[4],angular[4],mass;};
struct FragmentState {uint32_t present;float pose[16];};
struct Request {uint32_t command,steps,hitCount,actor;union {Hit hits[MaxHits];Advance advance;Mesh mesh;InitialPose initial;Target target;Fragment fragment;uint32_t active;};};
struct ActorState {
    uint32_t present,activeControllers;
    float poses[PartCount][16];
    float centerOfMass[4],velocity[4];
    uint32_t feedbackBySlot[6];
    uint32_t recoveryActive,recoveryCompleted,recoveryInterrupted;float recoveryTime;
    uint32_t supportMask;float footSpeed,trunkAngularSpeed;uint32_t detachedMask;
};
struct Response {
    uint32_t status,frame,parts,activeControllers;
    float poses[PartCount][16];
    float centerOfMass[4],velocity[4];
    uint32_t feedbackBySlot[6];
    uint32_t recoveryActive,recoveryCompleted,recoveryInterrupted;float recoveryTime;
};
struct Exchange {
    uint32_t magic,version,bytes,ownerPid,flags;
    volatile int32_t requested,completed;
    Request request;
    Response response;
    ActorState actors[MaxActors];
    Rig rig;
    FragmentState fragments[MaxFragments];
};
static_assert(sizeof(Hit)==72,"IPC hit layout");
static_assert(MaxActors<=32,"actor target bit mask");
static_assert(offsetof(Exchange,request)==28,"IPC header layout");
static_assert(sizeof(Response)==1432,"IPC response layout");
static_assert(sizeof(ActorState)==1440,"IPC actor layout");
static_assert(sizeof(Shape)==64,"IPC collision shape layout");
static_assert(sizeof(Exchange)<65536,"bounded shared exchange");
}
