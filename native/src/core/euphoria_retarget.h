#pragma once
#include "euphoria_math.h"

namespace euphoria {

// Retarget rotations, not foreign joint positions. Rebuilding the original
// hierarchy keeps skinning offsets and limb lengths intact, including during
// the animation/physics blend. Parents must precede children; duplicate bones
// are represented as zero-length children of the original bone.
inline void retargetHierarchy(int count, const short *parent, const short *body,
    int root, const Vec3 *animPos, const Quat *animQ, const Quat *bodyQ,
    Vec3 rootPos, float blend, Vec3 *outPos, Quat *outQ, const Vec3 *skinOffset = nullptr,
    const short *duplicate = nullptr)
{
    blend = clampf(blend, 0.0f, 1.0f);
    for ( int i = 0; i < count; ++i )
    {
        if ( duplicate && duplicate[i] >= 0 && duplicate[i] < i )
        {
            outPos[i] = outPos[duplicate[i]];
            outQ[i] = outQ[duplicate[i]];
            continue;
        }
        int p = parent[i];
        Quat localQ = p >= 0 ? conj(animQ[p]) * animQ[i] : animQ[i];
        Quat inherited = p >= 0 ? outQ[p] * localQ : animQ[i];
        outQ[i] = qnormalize(body[i] >= 0 ? qslerp(inherited, bodyQ[body[i]], blend) : inherited);
        if ( i == root )
            outPos[i] = lerp(animPos[i], rootPos, blend);
        else if ( p >= 0 )
        {
            Vec3 offset = rotateInv(animQ[p], animPos[i] - animPos[p]);
            if ( skinOffset ) offset = lerp(offset, skinOffset[i], blend);
            outPos[i] = outPos[p] + rotate(outQ[p], offset);
        }
        else
            outPos[i] = animPos[i];
    }
}

} // namespace euphoria
