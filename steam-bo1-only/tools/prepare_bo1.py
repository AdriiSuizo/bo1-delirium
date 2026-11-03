"""First-run preparation from a local BO1 capture; no GTA installation used."""
import argparse
import hashlib
import json
from pathlib import Path
from build_bo1_character import build as character
from build_bo1_motions import build as motions


def prepare(capture,output):
    skeletons=sorted(capture.glob('c_*body*.json'))
    if not skeletons:
        raise ValueError('No original BO1 zombie was captured; preparation was not completed')
    preferred=next((p for p in skeletons if 'body2' in p.name),skeletons[0])
    definition=capture/'bo1-ragdoll.json'
    animation=capture/'animations'
    if not definition.is_file() or not animation.is_dir():
        raise ValueError('Incomplete local BO1 capture')
    output.mkdir(parents=True,exist_ok=True)
    character(animation,preferred,definition,output/'bo1-character.json')
    motions(animation,json.loads(preferred.read_text()),output/'bo1-getup.motion')
    profile=json.loads((output/'bo1-character.json').read_text())
    if len(profile['parts'])!=21 or len(profile['joints'])!=20:
        raise ValueError('Incomplete BO1 articulation')
    marker={'format':'bo1-independent-v1','model':profile['provenance']['source'],
            'gta_files_required':False,'files':{}}
    for name in ['bo1-character.json','bo1-getup.motion']:
        marker['files'][name]=hashlib.sha256((output/name).read_bytes()).hexdigest()
    # The launcher only accepts this marker after every resource succeeded.
    (output/'bo1-independent-v1.ready').write_text(json.dumps(marker,indent=2)+'\n')
    print('BO1-only local resources ready. Do not redistribute this game-data cache.')


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--capture',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    try:
        prepare(a.capture,a.output)
    except Exception as error:
        p.exit(1,f'BO1 resource preparation failed: {error}\n')
