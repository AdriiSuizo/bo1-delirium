"""Generate a private 21-link NM character from the user's BO1 capture.

Dimensions, total and regional masses, collision pairs and native joint limits
come from BO1. Additional links split the native regions by their measured
shape volumes, preserving each region's mass. Movement envelopes come from
the captured BO1 get-ups. No Fred, XRT, ART or GTA fragment is read.
"""
from pathlib import Path
import json, math, argparse
from build_bo1_motions import pose, mul, normal, rotate, NAMES
from decode_xanim import tracks, verify_native

PARENTS=[None,0,1,2,0,4,5,0,7,8,9,10,11,10,13,14,15,10,17,18,19]
OWNERS=[0,8,9,9,10,11,11,1,2,2,2,3,3,4,4,5,5,6,6,7,7]
LABELS=['Pelvis','L_Thigh','L_Calf','L_Foot','R_Thigh','R_Calf','R_Foot','Spine','Spine1','Spine2','Spine3','Neck','Head','L_Clavicle','L_UpperArm','L_Forearm','L_Hand','R_Clavicle','R_UpperArm','R_Forearm','R_Hand']
IDENTITY=[0,0,0,1]
C=[0,0,-math.sqrt(.5),math.sqrt(.5)]

def add(a,b): return [x+y for x,y in zip(a,b)]
def sub(a,b): return [x-y for x,y in zip(a,b)]
def scale(a,s): return [x*s for x in a]
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def length(a): return math.sqrt(dot(a,a))
def unit(a):
    n=length(a)
    if n<1e-8: raise ValueError('zero-length anatomical segment')
    return scale(a,1/n)
def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def inverse(q): return [-q[0],-q[1],-q[2],q[3]]
def ours(a): return [-a[0],-a[1],a[2]]
def canonical(a): return scale(rotate(C,a),1/39.37)
def columns(q): return [rotate(q,[1,0,0]),rotate(q,[0,1,0]),rotate(q,[0,0,1])]
def quaternion(columns):
    m=[[columns[j][i] for j in range(3)] for i in range(3)]
    trace=sum(m[i][i] for i in range(3))
    if trace>0:
        s=math.sqrt(trace+1)*2
        return normal([(m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s/4])
    i=max(range(3),key=lambda i:m[i][i]);j=(i+1)%3;k=(i+2)%3
    s=math.sqrt(1+m[i][i]-m[j][j]-m[k][k])*2
    q=[0,0,0,(m[k][j]-m[j][k])/s];q[i]=s/4;q[j]=(m[j][i]+m[i][j])/s;q[k]=(m[k][i]+m[i][k])/s
    return normal(q)
def frame(along,arm=False):
    y=unit(along);seed=[0,-1,0] if arm else [1,0,0]
    x=unit(sub(seed,scale(y,dot(seed,y))))
    return quaternion([x,y,cross(x,y)])
def angles(q,three):
    if q[3]<0:q=scale(q,-1)
    if not three:return [2*math.atan2(q[2],q[3]),0,0]
    twist=normal([0,0,q[2],q[3]])
    swing=mul(q,inverse(twist));n=math.hypot(swing[0],swing[1])
    k=2*math.atan2(n,swing[3])/n if n>1e-9 else 0
    return [swing[1]*k,-swing[0]*k,2*math.atan2(twist[2],twist[3])]

def build(directory,skeleton_file,ragdoll_file,output):
    skeleton=json.loads(skeleton_file.read_text());definition=json.loads(ragdoll_file.read_text())['definitions'][1]
    native=definition['bones'];joint_by_bone={j['bone']:j for j in definition['joints']}
    bind=pose(skeleton,{},0)
    by_name={}
    # Complete bind hierarchy, including the toe/finger tips used for bounds.
    world=[]
    for b in skeleton['bones']:
        pp,pq=world[b['parent']] if b['parent']>=0 else ([0,0,0],IDENTITY)
        world.append((add(pp,rotate(pq,b['local_position'])),normal(mul(pq,normal(b['local_rotation'])))))
        by_name.setdefault(b['name'],world[-1])
    point=lambda name:canonical(by_name[name][0])
    mid=lambda a,b:scale(add(a,b),.5)
    neck_mid=mid(point('j_spine4'),point('j_neck'))
    seg=[(point('j_mainroot'),point('j_spinelower')),
         (point('j_hip_le'),point('j_knee_le')),(point('j_knee_le'),point('j_ankle_le')),(point('j_ankle_le'),point('j_ball_le')),
         (point('j_hip_ri'),point('j_knee_ri')),(point('j_knee_ri'),point('j_ankle_ri')),(point('j_ankle_ri'),point('j_ball_ri')),
         (point('j_spinelower'),point('j_spineupper')),(point('j_spineupper'),point('j_spine4')),
         (point('j_spine4'),neck_mid),(neck_mid,point('j_neck')),
         (point('j_neck'),point('j_head')),(point('j_head'),point('j_head_end')),
         (point('j_clavicle_le'),point('j_shoulder_le')),(point('j_shoulder_le'),point('j_elbow_le')),
         (point('j_elbow_le'),point('j_wrist_le')),(point('j_wrist_le'),point('j_mid_le_1')),
         (point('j_clavicle_ri'),point('j_shoulder_ri')),(point('j_shoulder_ri'),point('j_elbow_ri')),
         (point('j_elbow_ri'),point('j_wrist_ri')),(point('j_wrist_ri'),point('j_mid_ri_1'))]
    qbind=[frame(sub(b,a),i>=13) for i,(a,b) in enumerate(seg)]
    # balance_solve's pelvis frame is X up, Y right, Z forward (its pdm),
    # while limb capsules use Y along the bone. BO1's pelvis bound is a sphere.
    qbind[0]=quaternion([[0,0,1],[-1,0,0],[0,-1,0]])
    # NmDbm support_at uses foot.a across and foot.c along the sole;
    # foot.b is up. The physical box therefore has Y height and Z length.
    # Capsule-Y bone frames are not a valid foot support frame.
    for i in [3,6]:qbind[i]=frame([0,0,1])
    centres=[mid(a,b) for a,b in seg];origin=centres[0]
    radii=[native[o]['radius']/39.37 for o in OWNERS]
    half=[max(0,length(sub(b,a))*.5-radii[i]) for i,(a,b) in enumerate(seg)]
    boxes=[None]*21
    for i in [3,6]:
        a,b=seg[i]
        boxes[i]=[radii[i],abs(a[2]-b[2])*.5,length(sub(a,b))*.5+radii[i]]
        if boxes[i][1]<1e-5:raise ValueError('foot has no measured sole height')
    volume=[8*math.prod(boxes[i]) if boxes[i] else math.pi*r*r*(2*h+4*r/3) for i,(r,h) in enumerate(zip(radii,half))]
    masses=[native[o]['mass_engine_units']*1000*volume[i]/sum(volume[k] for k in range(21) if OWNERS[k]==o) for i,o in enumerate(OWNERS)]
    total=sum(masses)
    assert abs(total-sum(b['mass_engine_units']*1000 for b in native))<1e-8
    meta,idle=tracks(directory/'ai_zombie_idle_base.json');verify_native(meta,idle)
    idlepose=pose(skeleton,idle,0)
    def transform(source):
        result=[]
        for i,((_,qb),(p,qa)) in enumerate(zip(bind,source)):
            d=normal(mul(mul(mul(C,qa),inverse(qb)),inverse(C)))
            q=normal(mul(d,qbind[i]))
            centre=add(canonical(p),rotate(d,sub(centres[i],canonical(bind[i][0]))))
            result.append((centre,q))
        return result
    initial=transform(idlepose)
    samples=[transform(pose(skeleton,curves,m['frames']*i/60))
             for path in directory.glob('*getup*.json') for m,curves in [tracks(path)] for i in range(61)]
    joints=[];limits=[]
    for child in range(1,21):
        parent=PARENTS[child];o=OWNERS[child]
        hinge=child in [2,5,15,19]
        axis=[-1,0,0] if child in [2,5] else ([0,0,1] if child==15 else [0,0,-1]) if hinge else unit(sub(seg[child][1],seg[child][0]))
        if child in [3,6]:axis=[0,-1,0]
        lean=[0,0,1] if child in [3,6] else unit(sub([0,-1,0],scale(axis,dot(axis,[0,-1,0])))) if abs(axis[1])<.99 else [1,0,0]
        f=quaternion([lean,cross(axis,lean),axis])
        pf=normal(mul(inverse(qbind[parent]),f));cf=normal(mul(inverse(qbind[child]),f))
        measured=[]
        for sample in samples:
            a=mul(sample[parent][1],pf);b=mul(sample[child][1],cf)
            measured.append(angles(mul(inverse(a),b),not hinge))
        extent=[[min(0,min(v[k] for v in measured)),max(0,max(v[k] for v in measured))] for k in range(3)]
        native_joint=joint_by_bone.get(o)
        if native_joint:
            if hinge and native_joint['type']==1:
                extent[0][0]=min(extent[0][0],native_joint['low'][0]);extent[0][1]=max(extent[0][1],native_joint['high'][0])
            elif native_joint['type']==2:
                for k,source_axis in enumerate([2,1,0]):
                    extent[k][0]=min(extent[k][0],native_joint['low'][source_axis]);extent[k][1]=max(extent[k][1],native_joint['high'][source_axis])
        # Degenerate unused axes remain locked; no invented extra mobility.
        if hinge:extent[1:]=[[0,0],[0,0]]
        reach=max(length(sub(c,seg[child][0])) for c in centres)
        budget=total*9.8*reach
        joints.append(dict(name=LABELS[child],index=child-1,three=not hinge,pos=ours(sub(seg[child][0],origin)),
                           twist_axis=ours(axis),lean_axis=ours(lean),limits=extent,
                           flip=[False,True,True] if child in [4,6,17,18,20] else [False]*3,
                           muscle_stiffness=5. if child in [3,6] else .5,
                           damping=20.,strength=100.,max_torque=[budget,budget]))
        limits.append(dict(parent_frame=pf,child_frame=cf))
    parts=[];physical=[]
    for i in range(21):
        # asset_frame(m,true) = R: m = R^T * diag(-1,-1,1).
        r=columns(qbind[i]);matrix=[[(-1 if k<2 else 1)*r[j][k] for j in range(3)] for k in range(3)]
        parts.append(dict(name=LABELS[i],frame=matrix+[ours(sub(centres[i],origin))],mass=masses[i]))
        p=PARENTS[i]
        physical.append(dict(name='Char_'+LABELS[i],parent=p,mass=masses[i],radius=radii[i],half=half[i],
            offset=dict(pos=[0,0,0],rot=IDENTITY),rest=IDENTITY if p is None else normal(mul(inverse(qbind[i]),qbind[p])),
            anchor_parent=[0,0,0] if p is None else rotate(inverse(qbind[p]),sub(seg[i][0],centres[p])),
            anchor=rotate(inverse(qbind[i]),sub(seg[i][0],centres[i])),limit={'locked':{}},lower=i<=6,
            reach=i in [14,15,18,19],bind=qbind[i],boxed=boxes[i],inertia=[0,0,0],
            frame=dict(pos=initial[i][0],rot=initial[i][1])))
    allowed={tuple(sorted(pair)) for pair in definition['self_collision']}
    pairs=[[a,b] for a in range(21) for b in range(a+1,21) if tuple(sorted([OWNERS[a],OWNERS[b]])) in allowed
           and PARENTS[b]!=a and PARENTS[a]!=b and a not in [13,17] and b not in [13,17]]
    result=dict(format='nm-character-1',joints=joints,parts=parts,build=physical,self_collision=pairs,
        provenance=dict(source='BO1 runtime skeleton and ragdoll definition 1',mass_kg=total,
          additional_links='measured-volume partition; native regional masses conserved',
          motor_defaults='NM effector calibration: strength 100, damping 20; ankle stiffness 5, other joints 0.5',
          motor_torque_limit='whole-body gravitational load at measured maximum reach',
          joint_envelope='union of BO1 native joint bounds and 61 samples per original get-up'))
    for p in physical:p['limit']='locked'
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(result,indent=2)+'\n')
    print('Generated BO1 character:',len(parts),'parts;',len(joints),'NM joints;',total,'kg;',len(pairs),'self pairs')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('skeleton',type=Path);p.add_argument('ragdoll',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();build(a.directory,a.skeleton,a.ragdoll,a.output)
