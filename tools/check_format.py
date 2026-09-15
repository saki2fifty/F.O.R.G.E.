from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parents[1]
files=sorted(str(p) for folder in ('include','src','samples','tests') for p in (root/folder).rglob('*') if p.suffix in ('.h','.hpp','.c','.cpp'))
raise SystemExit(subprocess.run(['clang-format','--dry-run','--Werror',*files]).returncode)
