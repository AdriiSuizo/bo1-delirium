"""Build local motor targets from verified BO1 tracks and a BO1 skeleton.

No GTA files are read. This cache is private game data, not a release asset.
Only semantic bone rotations and pelvis travel are transferred: the live
solver retains the target body's lengths, limits, contacts and velocities.
"""
import argparse
import json
import math
import struct
from pathlib import Path
from decode_xanim import tracks, sample, verify_native

NAMES = ["j_mainroot", "j_hip_le", "j_knee_le", "j_ankle_le", "j_hip_ri", "j_knee_ri", "j_ankle_ri",
         "j_spinelower", "j_spineupper", "j_spine4", "j_spine4", "j_neck", "j_head", "j_clavicle_le",
         "j_shoulder_le", "j_elbow_le", "j_wrist_le", "j_clavicle_ri", "j_shoulder_ri", "j_elbow_ri", "j_wrist_ri"]


def mul(a, b):
    x,y,z,w = a
    X,Y,Z,W = b
    return [w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X, w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z]


def normal(q):
    n = math.sqrt(sum(v*v for v in q))
    if n < .1 or not math.isfinite(n):
        raise ValueError("invalid source rotation")
    return [v/n for v in q]


def slerp(a,b,t):
    d=sum(x*y for x,y in zip(a,b))
    if d<0:b=[-v for v in b];d=-d
    if d>.9995:return normal([x+(y-x)*t for x,y in zip(a,b)])
    angle=math.acos(max(-1,min(1,d)));s=math.sin(angle)
    return normal([(math.sin((1-t)*angle)*x+math.sin(t*angle)*y)/s for x,y in zip(a,b)])


def rotate(q, p):
    return mul(mul(q, list(p)+[0]), [-q[0],-q[1],-q[2],q[3]])[:3]


def pose(skeleton, curves, at, root=None):
    world = []
    for bone in skeleton["bones"]:
        curve = curves.get(bone["name"])
        delta = sample(curve["position"], at) if curve else [0,0,0]
        q = sample(curve["rotation"], at, True) if curve else normal(bone["local_rotation"])
        p = [a+b for a,b in zip(bone["local_position"],delta)]
        parent = bone["parent"]
        if parent >= len(world):
            raise ValueError("non-topological BO1 hierarchy")
        pp,pq = world[parent] if parent >= 0 else ([0,0,0],[0,0,0,1])
        world.append(([a+b for a,b in zip(pp,rotate(pq,p))],normal(mul(pq,q))))
    for child, parent in skeleton.get("duplicates", []):
        if not 0 <= parent < child < len(world):
            raise ValueError("invalid duplicate-bone reference")
        world[child] = world[parent]
    # A DObj's attachment model can repeat a name; the first body occurrence
    # owns the physical bone. Face/hair attachment transforms are not bones
    # of another articulated body.
    mapped = {}
    for bone, transform in zip(skeleton["bones"],world):
        mapped.setdefault(bone["name"],transform)
    missing = set(NAMES)-mapped.keys()
    if missing:
        raise ValueError(f"missing physical bones: {sorted(missing)}")
    transforms = [mapped[name] for name in NAMES]
    # BO1 has three spine segments; NM has four. Split the last rotation
    # continuously instead of making the extra joint a degenerate 3-DOF joint.
    transforms[9]=(transforms[9][0],slerp(transforms[8][1],transforms[10][1],.5))
    if root:
        t,q = [0,0,0],[0,0,0,1]
        if root.get("position"):
            track = root["position"]
            t = sample([track["times"],track["values"]],at)
        if root.get("rotation"):
            track = root["rotation"]
            q = sample([track["times"],track["values"]],at,True)
        transforms = [([a+b for a,b in zip(t,rotate(q,p))],normal(mul(q,r))) for p,r in transforms]
    return transforms


def build(directory, skeleton, output):
    meta, idle = tracks(directory/"ai_zombie_idle_base.json")
    verify_native(meta,idle)
    reference = pose(skeleton,idle,0)
    clips = []
    for path in sorted(directory.glob("*getup*.json")):
        meta, curves = tracks(path)
        verify_native(meta,curves)
        count = math.ceil(meta["frames"]/meta["fps"]*60)+1
        frames = []
        for i in range(count):
            at = meta["frames"]*i/(count-1)
            full = pose(skeleton,curves,at,meta.get("root_delta"))
            pelvis = [(a-b)/39.37 for a,b in zip(full[0][0],reference[0][0])]
            frames.append((pelvis,[q for _,q in full]))
        first = frames[0][1][0]
        rest = reference[0][1]
        # The model faces +X. Its physical chest-facing vector tells us which
        # side is down; no random front/back clip duplication is introduced.
        facing = rotate(mul(first,[-rest[0],-rest[1],-rest[2],rest[3]]),[1,0,0])
        back = facing[2] > 0
        variant = "fast" if "quick" in path.stem else "normal"
        name = ("get_up_back/" if back else "get_up/")+path.stem+"_"+variant
        clips.append((name,meta["frames"]/meta["fps"],frames))
        print(path.stem,"back" if back else "front",variant,count,"frames")
    if not all(any(name.startswith(prefix) and name.endswith("_normal") for name,_,_ in clips)
               for prefix in ["get_up/","get_up_back/"]):
        raise ValueError("BO1 source needs both front and back recoveries")
    data = bytearray(b"BO1MOT01"+struct.pack("<II",21,len(clips)))
    for _,q in reference:
        data += struct.pack("<4f",*q)
    for name,duration,frames in clips:
        encoded = name.encode()
        data += struct.pack("<I",len(encoded))+encoded+struct.pack("<If",len(frames),duration)
        for p,qs in frames:
            data += struct.pack("<3f",*p)
            for q in qs:
                data += struct.pack("<4f",*q)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_bytes(data)
    print("Private BO1 recovery cache:",len(data),"bytes; no GTA animations or idle file used")


if __name__ == "__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("directory",type=Path)
    parser.add_argument("skeleton",type=Path)
    parser.add_argument("output",type=Path)
    a=parser.parse_args()
    build(a.directory,json.loads(a.skeleton.read_text()),a.output)
