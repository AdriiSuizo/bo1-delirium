#pragma once
// mod: euphoria - engine-independent math for the active-ragdoll core (src/euphoria). NOT part of the original game.
// Plain C++17, no engine headers: the same files build in the game (MSVC x86) and in the tests (g++), and can be
// wrapped from Rust later. Units are the game's (inches, z up, degrees only at the API edge).
#include <cmath>

namespace euphoria {

struct Vec3
{
    float x, y, z;
};

inline Vec3 v3(float x, float y, float z) { Vec3 v = { x, y, z }; return v; }
inline Vec3 operator+(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator-(Vec3 a) { return v3(-a.x, -a.y, -a.z); }
inline Vec3 operator*(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
inline Vec3 operator*(float s, Vec3 a) { return v3(a.x * s, a.y * s, a.z * s); }
inline Vec3 &operator+=(Vec3 &a, Vec3 b) { a = a + b; return a; }
inline Vec3 &operator-=(Vec3 &a, Vec3 b) { a = a - b; return a; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline float length(Vec3 a) { return sqrtf(dot(a, a)); }
inline float lengthSq(Vec3 a) { return dot(a, a); }
inline Vec3 normalize(Vec3 a) { float l = length(a); return l > 1e-8f ? a * (1.0f / l) : v3(0, 0, 0); }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline bool finite3(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// Quaternion x, y, z, w (the engine's DObjAnimMat order): q rotates body-frame vectors into the world.
struct Quat
{
    float x, y, z, w;
};

inline Quat q4(float x, float y, float z, float w) { Quat q = { x, y, z, w }; return q; }
inline Quat qIdentity() { return q4(0, 0, 0, 1); }
inline Quat operator*(Quat a, Quat b) // a then b applied? No: (a*b) v = a (b v), the usual Hamilton product
{
    return q4(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
              a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
              a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
              a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
inline Quat conj(Quat q) { return q4(-q.x, -q.y, -q.z, q.w); }
inline float qdot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Quat qnormalize(Quat q)
{
    float l = sqrtf(qdot(q, q));
    if ( l < 1e-8f )
        return qIdentity();
    float inv = 1.0f / l;
    return q4(q.x * inv, q.y * inv, q.z * inv, q.w * inv);
}
inline Vec3 rotate(Quat q, Vec3 v)
{
    // v' = v + 2 w (u x v) + 2 u x (u x v), u = (x, y, z)
    Vec3 u = v3(q.x, q.y, q.z);
    Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}
inline Vec3 rotateInv(Quat q, Vec3 v) { return rotate(conj(q), v); }
inline Quat qFromAxisAngle(Vec3 axis, float angle)
{
    Vec3 a = normalize(axis);
    float s = sinf(angle * 0.5f);
    return q4(a.x * s, a.y * s, a.z * s, cosf(angle * 0.5f));
}
// the rotation that takes unit vector a to unit vector b
inline Quat qFromTo(Vec3 a, Vec3 b)
{
    float d = dot(a, b);
    Vec3 c = cross(a, b);
    float w = 1.0f + d;
    if ( w < 1e-6f )
    {
        // opposite: any perpendicular axis
        Vec3 axis = fabsf(a.x) < 0.9f ? cross(a, v3(1, 0, 0)) : cross(a, v3(0, 1, 0));
        return qFromAxisAngle(axis, 3.14159265f);
    }
    return qnormalize(q4(c.x, c.y, c.z, w));
}
// rotation vector (axis * angle) of q, shortest arc
inline Vec3 qToRotVec(Quat q)
{
    if ( q.w < 0.0f )
        q = q4(-q.x, -q.y, -q.z, -q.w);
    float s = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z);
    if ( s < 1e-6f )
        return v3(0, 0, 0);
    float angle = 2.0f * atan2f(s, q.w);
    return v3(q.x, q.y, q.z) * (angle / s);
}
inline Quat qFromRotVec(Vec3 r)
{
    float angle = length(r);
    if ( angle < 1e-6f )
        return qnormalize(q4(r.x * 0.5f, r.y * 0.5f, r.z * 0.5f, 1.0f));
    return qFromAxisAngle(r, angle);
}
inline Quat qslerp(Quat a, Quat b, float t)
{
    if ( qdot(a, b) < 0.0f )
        b = q4(-b.x, -b.y, -b.z, -b.w);
    // nlerp is enough for the small steps here
    return qnormalize(q4(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t), lerpf(a.w, b.w, t)));
}
// integrate angular velocity (world, rad/s) over dt
inline Quat qIntegrate(Quat q, Vec3 omega, float dt)
{
    Quat dq = q4(omega.x * dt * 0.5f, omega.y * dt * 0.5f, omega.z * dt * 0.5f, 0.0f);
    Quat r = dq * q;
    return qnormalize(q4(q.x + r.x, q.y + r.y, q.z + r.z, q.w + r.w));
}
inline bool finiteq(Quat q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }

// quaternion of the rotation whose columns are the world directions of the local x, y, z axes
// (the engine's AnglesToAxis gives axis[0] = forward (local x), axis[1] = left (local y), axis[2] = up (local z))
inline Quat qFromAxes(Vec3 ax, Vec3 ay, Vec3 az)
{
    // matrix M with columns ax, ay, az: m00 = ax.x, m01 = ay.x, m02 = az.x, m10 = ax.y ...
    float m00 = ax.x, m01 = ay.x, m02 = az.x;
    float m10 = ax.y, m11 = ay.y, m12 = az.y;
    float m20 = ax.z, m21 = ay.z, m22 = az.z;
    float tr = m00 + m11 + m22;
    Quat q;
    if ( tr > 0.0f )
    {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    }
    else if ( m00 > m11 && m00 > m22 )
    {
        float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    }
    else if ( m11 > m22 )
    {
        float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    }
    else
    {
        float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return qnormalize(q);
}

const float PI = 3.14159265f;
const float DEG2RAD = 0.017453292f;
const float RAD2DEG = 57.29578f;

} // namespace euphoria
