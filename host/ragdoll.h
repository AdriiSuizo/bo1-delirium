/* C interface of the ragdoll engine (crates/ragdoll_ffi).
 *
 * Coordinates are the host's, declared once in RdWorldDesc: e.g. BO1 uses
 * inches (39.37 per metre) and RD_AXES_Z_UP (+X forward, +Y left, +Z up).
 * rd_world_create() uses the engine's own: metres, +Y up, -Z forward.
 * Impulses are in N*s and angular velocities in rad/s whatever the units.
 * No function unwinds across the boundary; errors are negative return codes. */
#ifndef RAGDOLL_H
#define RAGDOLL_H
#include <stdint.h>
#ifndef RD_API
#define RD_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

#define RD_OK 0
#define RD_ERR_ARG (-1)
#define RD_ERR_LOAD (-2)
#define RD_ERR_PANIC (-3)
#define RD_ERR_NOT_FOUND (-4)

#define RD_AXES_ENGINE 0u
#define RD_AXES_Z_UP 1u

#define RD_MODE_PASSIVE 0u /* limp ragdoll (dead) */
#define RD_MODE_ACTIVE 1u  /* muscles: balance, steps, reactions, falls, getting up */

#define RD_STATE_FALLING 1u
#define RD_STATE_DOWN 2u
#define RD_STATE_GETTING_UP 4u
#define RD_STATE_GAVE_UP 8u
#define RD_STATE_STEPPING 16u
#define RD_STATE_BALANCED 32u /* standing still: safe to hand back to an animation */
#define RD_STATE_ASLEEP 64u
#define RD_STATE_UPRIGHT 128u /* supported and upright, including active walking */
/* Has used the verified GTA 1.0.8 double-support posture component. */
#define RD_STATE_NM_POSTURE 256u
#define RD_STATE_NM_SINGLE_SUPPORT 1024u
#define RD_STATE_NM_SHOT_SPINE 512u
#define RD_STATE_NM_WOUND 2048u
#define RD_STATE_NM_FOOT_PLANNING 8192u
#define RD_STATE_NM_FOOT_CONTACT 16384u
#define RD_STATE_NM_BALL_MOTOR 32768u
#define RD_STATE_NM_LEG_CLEARANCE 65536u
#define RD_STATE_RECOVERY_MOTION 4096u

/* RdActorState.get_up_stage */
#define RD_GETUP_ROLL 0
#define RD_GETUP_HANDS 1
#define RD_GETUP_PUSH_UP 2
#define RD_GETUP_QUAD 3
#define RD_GETUP_UPRIGHT 4
#define RD_GETUP_BEAR 5
#define RD_GETUP_CROUCH 6
#define RD_GETUP_SHIFT 7
#define RD_GETUP_FOOT_FORWARD 8
#define RD_GETUP_EXTEND 9
#define RD_GETUP_KNEEL 10
#define RD_GETUP_RISE 11

#define RD_BODY_COUNT 16u

typedef struct RdWorld RdWorld;
typedef struct { float x, y, z; } RdVec3;
typedef struct { float x, y, z, w; } RdQuat;
typedef struct { RdVec3 position; RdQuat rotation; } RdTransform;
/* body: 0..15 (rd_body_name); point in host units; impulse in N*s. */
typedef struct { uint32_t body; RdVec3 point; RdVec3 impulse; } RdHit;
typedef struct { float units_per_meter; uint32_t axes; } RdWorldDesc;
typedef struct {
    uint32_t mode;         /* RD_MODE_* */
    uint32_t flags;        /* RD_STATE_* */
    int32_t get_up_stage;  /* RD_GETUP_*, -1 when not getting up */
    uint32_t steps;        /* recovery steps taken */
    RdVec3 com;            /* centre of mass, host units */
    RdVec3 com_velocity;   /* host units/s */
    float tilt;            /* trunk tilt from vertical, rad */
    float pain;            /* recent hits, N*s, decays */
} RdActorState;

RD_API RdWorld *rd_world_create(void);
RD_API RdWorld *rd_world_create_ex(const RdWorldDesc *desc);
RD_API void rd_world_destroy(RdWorld *w);
/* Optional bridge exports: resolve dynamically for older engine compatibility.
 * Bit 0: full implemented Delirium backend. Bit 1: continuous host locomotion. */
RD_API uint32_t rd_world_capabilities(const RdWorld *w);
/* Delirium message name followed by key=value parameters; vectors use x,y,z.
 * Unknown names/parameters return RD_ERR_ARG without invalidating the world. */
RD_API int32_t rd_actor_message(RdWorld *w, uint32_t actor, const char *message_utf8);
/* Tuning from RON text (testbed params.ron format). */
RD_API int32_t rd_world_set_params_ron(RdWorld *w, const char *ron_utf8);
RD_API int32_t rd_load_art(RdWorld *w, const char *path_utf8, uint32_t *out_model);
/* Rest pose of the 16 bodies with the root at the origin (bone mapping). */
RD_API int32_t rd_model_rest_bodies(const RdWorld *w, uint32_t model, RdTransform *out16);
/* Each body's rest segment: start = joint it hangs from (pelvis: origin), end = main child's joint or far end. */
RD_API int32_t rd_model_rest_segments(const RdWorld *w, uint32_t model, RdVec3 *out_start16, RdVec3 *out_end16);
RD_API int32_t rd_add_static_mesh(RdWorld *w, const float *vertices, uint32_t vertex_count, const uint32_t *indices, uint32_t triangle_count);
/* A static convex volume (map brush) from count xyz points (>= 4). */
RD_API int32_t rd_add_static_convex(RdWorld *w, const float *points, uint32_t count);
/* Spawns in the rest pose placed at root (a passive ragdoll until set_mode). */
RD_API int32_t rd_actor_spawn(RdWorld *w, uint32_t model, const RdTransform *root, RdVec3 velocity, uint32_t *out_actor);
RD_API int32_t rd_actor_despawn(RdWorld *w, uint32_t actor);
RD_API int32_t rd_actor_set_mode(RdWorld *w, uint32_t actor, uint32_t mode);
/* Hand an animated pose to physics: 16 body frames + velocities (may be null). */
RD_API int32_t rd_actor_set_bodies(RdWorld *w, uint32_t actor, const RdTransform *bodies16, const RdVec3 *linear16, const RdVec3 *angular16);
/* Place from the pelvis body frame + world rotation of every body; positions follow the joints. */
RD_API int32_t rd_actor_set_pose_rotations(RdWorld *w, uint32_t actor, const RdTransform *pelvis, const RdQuat *rotations16, RdVec3 linvel);
RD_API int32_t rd_actor_hit(RdWorld *w, uint32_t actor, const RdHit *hit);
RD_API int32_t rd_actor_set_motion(RdWorld *w, uint32_t actor, RdVec3 velocity, RdVec3 forward);
RD_API int32_t rd_actor_set_reach(RdWorld *w, uint32_t actor, float reach);
/* Advances by dt seconds in fixed steps; returns the number of steps taken. */
RD_API int32_t rd_world_update(RdWorld *w, float dt);
RD_API int32_t rd_actor_get_bodies(const RdWorld *w, uint32_t actor, RdTransform *out, uint32_t capacity);
RD_API int32_t rd_actor_get_velocities(const RdWorld *w, uint32_t actor, RdVec3 *out_linear16, RdVec3 *out_angular16);
RD_API int32_t rd_actor_get_state(const RdWorld *w, uint32_t actor, RdActorState *out);
/* World position of the joint each body hangs from (pelvis: its origin). */
RD_API int32_t rd_actor_get_joint_positions(const RdWorld *w, uint32_t actor, RdVec3 *out16);
RD_API uint32_t rd_body_count(void);
RD_API const char *rd_body_name(uint32_t body);

#ifdef __cplusplus
}
#endif
#endif
