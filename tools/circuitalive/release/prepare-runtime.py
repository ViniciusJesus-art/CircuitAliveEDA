"""Install matching XSPICE code models next to an isolated worker for testing."""
from pathlib import Path
import shutil
import sys
import os
worker=Path(sys.argv[1]);source=Path(sys.argv[2])
models=worker/'ngspice';models.mkdir(parents=True,exist_ok=True)
for file in source.glob('*.cm'):
    if file.resolve()!=(models/file.name).resolve():shutil.copy2(file,models/file.name)
scripts=models/'scripts';scripts.mkdir(exist_ok=True)
(scripts/'spinit').write_text('* CircuitAliveEDA test runtime\nunset osdi_enabled\nset num_threads=2\n'+''.join(
    f"codemodel ngspice/{file.name}\n" if os.name=='nt' else f"codemodel '{file.resolve().as_posix()}'\n"
    for file in models.glob('*.cm')))
