"""Decode locally captured BO1 XAnim tracks; no GTA data or game keys.

The file has separate streams, not interleaved per-bone records. All streams
must be consumed exactly: a layout mismatch is an error, never a partial clip.
The root-motion delta is separate and is deliberately not fabricated here.
"""
import argparse
import bisect
import json
import math
import struct
from pathlib import Path


class Stream:
    def __init__(self, values):
        self.values, self.offset = values, 0

    def take(self, n):
        if n < 0 or self.offset + n > len(self.values):
            raise ValueError("truncated XAnim stream")
        out = self.values[self.offset:self.offset+n]
        self.offset += n
        return list(out)

    def one(self):
        return self.take(1)[0]

    def finish(self):
        if self.offset != len(self.values):
            raise ValueError(f"unconsumed XAnim stream: {self.offset}/{len(self.values)}")


def tracks(path):
    meta = json.loads(path.read_text(encoding="utf-8"))
    if meta["format"] != "bo1-xanim-buffers-1":
        raise ValueError("unsupported capture format")
    raw = path.with_suffix(".bin").read_bytes()
    offset, streams = 0, []
    kinds = ["B", "h", "f", "h", "B", "i", "B" if meta["frames"] < 256 else "H"]
    for n, kind in zip(meta["buffer_counts"], kinds):
        size = struct.calcsize(kind) * n
        if offset + size > len(raw):
            raise ValueError("truncated XAnim buffers")
        streams.append(Stream(struct.unpack_from("<"+str(n)+kind, raw, offset)))
        offset += size
    if offset != len(raw):
        raise ValueError("trailing XAnim buffers")
    byte, short, real, rshort, rbyte, rint, indices = streams
    counts, names = meta["bone_count"], meta["names"]
    if sum(counts[:5]) != len(names) or sum(counts[5:9]) != len(names):
        raise ValueError("XAnim rotation/translation bone totals do not match")
    result = {name: {} for name in names}

    def keys(n):
        if meta["frames"] < 256:
            times = byte.take(n+1)
        elif n < 64:
            times = [v & 65535 for v in short.take(n+1)]
        else:
            short.take(((n-1) >> 8)+2)  # coarse time lookup; fine table follows
            times = indices.take(n+1)
        if times != sorted(set(times)) or times[0] != 0 or times[-1] != meta["frames"]:
            raise ValueError("invalid XAnim key times")
        return times

    bone = 0
    for category, count in enumerate(counts[:5]):
        for _ in range(count):
            name = names[bone]
            bone += 1
            if category == 0:
                times, values = [0], [[0, 0, 0, 1]]
            else:
                dimensions = 2 if category in (1, 3) else 4
                dynamic = category in (1, 2)
                n = short.one() & 65535 if dynamic else 0
                times = keys(n) if dynamic else [0]
                data = (rshort if dynamic else short).take(dimensions*(n+1))
                values = []
                for i in range(n+1):
                    value = [v / 32767.0 for v in data[i*dimensions:(i+1)*dimensions]]
                    if dimensions == 2:
                        value = [0, 0]+value
                    if not all(math.isfinite(v) for v in value) or sum(v*v for v in value) < .5:
                        raise ValueError("invalid XAnim quaternion")
                    values.append(value)
            result[name]["rotation"] = [times, values]

    for category, count in enumerate(counts[5:9]):
        for _ in range(count):
            bone = byte.one()
            if bone >= len(names) or "position" in result[names[bone]]:
                raise ValueError("invalid/repeated XAnim translation bone")
            if category < 2:
                n = short.one() & 65535
                times = keys(n)
                minima, scale = real.take(3), real.take(3)
                data = (rbyte if category == 0 else rshort).take(3*(n+1))
                if category == 1:
                    data = [v & 65535 for v in data]
                values = [[minima[k]+scale[k]*data[3*i+k] for k in range(3)] for i in range(n+1)]
            else:
                times, values = [0], [real.take(3) if category == 2 else [0, 0, 0]]
            if not all(math.isfinite(v) for frame in values for v in frame):
                raise ValueError("nonfinite XAnim translation")
            result[names[bone]]["position"] = [times, values]
    for stream in streams:
        stream.finish()
    if meta.get("root_delta"):
        for kind, track in meta["root_delta"].items():
            if track:
                times, values = track["times"], track["values"]
                if not times or len(times) != len(values) or times != sorted(set(times)):
                    raise ValueError("invalid root delta keys")
                if not all(math.isfinite(v) for row in values for v in row):
                    raise ValueError("invalid root delta value")
                if any(len(row) != (4 if kind == "rotation" else 3) for row in values):
                    raise ValueError("invalid root delta dimensions")
    return meta, result


def sample(track, at, rotation=False):
    times, values = track
    if len(times) == 1:
        out = values[0][:]
    else:
        i = max(0, min(len(times)-2, bisect.bisect_right(times, at)-1))
        t = max(0, min(1, (at-times[i])/(times[i+1]-times[i])))
        # BO1 linearly interpolates the packed quaternion, then normalizes
        # during accumulation. Do not replace it with a different slerp curve.
        out = [a+(b-a)*t for a, b in zip(values[i], values[i+1])]
    if rotation:
        length = math.sqrt(sum(v*v for v in out))
        if length < .1:
            raise ValueError("degenerate interpolated XAnim quaternion")
        out = [v/length for v in out]
    return out


def decode(path, fps=60, skeleton=None):
    meta, curves = tracks(path)
    count = math.ceil(meta["frames"] / meta["fps"] * fps)+1
    poses = []
    for i in range(count):
        at = min(meta["frames"], i/fps*meta["fps"])
        pose = {name: [sample(track["position"], at), sample(track["rotation"], at, True)]
                for name, track in curves.items()}
        if skeleton:
            # DObjCalcSkel adds the model's bind translation to the animated
            # translation delta. Rotations are absolute local rotations.
            # Replacing bind positions with zero deltas collapses all limbs.
            full = []
            for bone in skeleton["bones"]:
                track = pose.get(bone["name"])
                delta, rotation = track if track else ([0, 0, 0], bone["local_rotation"])
                position = [a+b for a, b in zip(bone["local_position"], delta)]
                full.append([position, rotation])
            poses.append(full)
        else:
            poses.append(pose)
    result = {"format": "bo1-skeletal-motion-1" if skeleton else "bo1-xanim-track-samples-1",
            "translation_mode": "absolute_local" if skeleton else "add_to_bind",
            "name": meta["name"], "fps": fps,
            "duration": meta["frames"]/meta["fps"], "root_delta_included": False,
            "local_poses": poses}
    if skeleton:
        result["model"] = skeleton["model"]
        result["bones"] = [{"name": b["name"], "parent": b["parent"]} for b in skeleton["bones"]]
        result["duplicates"] = skeleton.get("duplicates", [])
    if meta.get("root_delta"):
        result["root_delta_included"] = True
        result["root_delta"] = meta["root_delta"]
    return result


def verify_native(meta, curves):
    if not meta.get("oracle"):
        raise ValueError("missing poses sampled by the original BO1 executable")
    max_position, max_rotation, comparisons = 0.0, 0.0, 0
    for row in meta["oracle"]:
        if len(row["pose"]) != len(meta["names"]):
            raise ValueError("incomplete original-engine pose")
        at = row["phase"] * meta["frames"]
        for name, (position, rotation) in zip(meta["names"], row["pose"]):
            expected_p = sample(curves[name]["position"], at)
            expected_q = sample(curves[name]["rotation"], at, True)
            norm = math.sqrt(sum(v*v for v in rotation))
            if not math.isfinite(norm) or norm < .1:
                raise ValueError("invalid original-engine rotation")
            rotation = [v/norm for v in rotation]
            max_position = max(max_position, max(abs(a-b) for a, b in zip(position, expected_p)))
            # q and -q represent the same rotation.
            max_rotation = max(max_rotation, min(max(abs(a-b) for a, b in zip(rotation, expected_q)),
                                                 max(abs(a+b) for a, b in zip(rotation, expected_q))))
            comparisons += 1
    # Packed data and the stock SIMD decoder use float32; this tool uses
    # float64 before writing the cache. Tolerances are in native game units.
    if max_position > .0002 or max_rotation > .000002:
        raise ValueError(f"stock XAnim mismatch: position={max_position}, rotation={max_rotation}")
    return {"name": meta["name"], "comparisons": comparisons,
            "max_position_error_bo1_units": max_position,
            "max_quaternion_component_error": max_rotation}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verify-native", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--skeleton", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    reports = []
    skeleton = json.loads(args.skeleton.read_text()) if args.skeleton else None
    if skeleton and skeleton["format"] != "bo1-retail-skeleton-1":
        raise ValueError("unsupported BO1 skeleton")
    for path in sorted(args.directory.glob("*.json")):
        if args.verify_native:
            meta, curves = tracks(path)
            reports.append(verify_native(meta, curves))
        clip = decode(path, skeleton=skeleton)
        (args.output/path.name).write_text(json.dumps(clip, separators=(",", ":"))+"\n", encoding="utf-8")
        print(clip["name"], f'{clip["duration"]:.3f}s', len(clip["local_poses"]), "samples; all buffers consumed")
    if args.report:
        if not reports:
            raise ValueError("no verified animations")
        args.report.write_text(json.dumps({"passed": True, "clips": reports}, indent=2)+"\n", encoding="utf-8")
