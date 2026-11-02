"""Fail closed on anything outside the reviewed source-only staging allowlist."""
from pathlib import Path
import re, subprocess, sys
root=Path(__file__).resolve().parent.parent
files=subprocess.check_output(['git','diff','--cached','--name-only','-z'],cwd=root).decode().split('\0')
allowed_roots={'host','worker','tests','scripts','overlay'}
allowed_top={'.gitignore','Cargo.toml','Cargo.lock','README.md','LICENSE','NOTICE','Play-SinglePlayer.cmd'}
extensions={'.h','.hpp','.cpp','.rs','.toml','.ps1','.psm1','.py','.cmake'}
for name in filter(None,files):
 p=Path(name)
 if name not in allowed_top and (p.parts[0] not in allowed_roots or (p.suffix not in extensions and p.name!='CMakeLists.txt')):
  sys.exit(f'REJECT unexpected staged path: {name}')
 data=subprocess.check_output(['git','show',f':{name}'],cwd=root)
 if len(data)>150_000 or b'\0' in data:sys.exit(f'REJECT binary/oversized file: {name}')
 for pattern in [rb'gh[pousr]_[A-Za-z0-9]{30,}',rb'github_pat_[A-Za-z0-9_]{30,}',rb'-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----']:
  if re.search(pattern,data):sys.exit(f'REJECT potential secret: {name}')
 print(f'{len(data):7d} {name}')
print('PASS staged source-only allowlist, text/size and secret-pattern audit')
