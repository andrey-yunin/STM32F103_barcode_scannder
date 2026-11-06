from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='scanner-reset-connected-') as directory:
    for name in ['host','regression']:
        executable=Path(directory)/name
        subprocess.run(['gcc','-std=c11','-O0','-g','-Wall','-Wextra','-Werror',
            '-Wno-unused-function','-Wno-unused-variable','-Wno-comment',
            '-I'+str(root/'include'),'-I'+str(root),str(root/(name+'.c')),'-o',str(executable)],check=True)
        subprocess.run([str(executable)],check=True)
