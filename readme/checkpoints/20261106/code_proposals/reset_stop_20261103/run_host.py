"""Replay candidate HAL/RTOS models; no target or project modification."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='scanner-stop-') as directory:
    for name,source in [('shutdown',root/'host.c'),('regression',root/'regression/host.c')]:
        executable=Path(directory)/name
        subprocess.run(['gcc','-std=c11','-O0','-g','-Wall','-Wextra','-Werror',
            '-Wno-unused-function','-Wno-unused-variable','-Wno-comment',
            '-I'+str(root/'include'),'-I'+str(root),str(source),'-o',str(executable)],check=True)
        subprocess.run([str(executable)],check=True)
