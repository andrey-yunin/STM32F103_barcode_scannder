from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='scanner-rx-reset-') as directory:
    executable=Path(directory)/'host'
    subprocess.run(['gcc','-std=c11','-O0','-g','-Wall','-Wextra','-Werror',
        '-Wno-unused-function','-Wno-unused-variable','-Wno-comment',
        '-I'+str(root/'include'),str(root/'host.c'),'-o',str(executable)],check=True)
    subprocess.run([str(executable)],check=True)
