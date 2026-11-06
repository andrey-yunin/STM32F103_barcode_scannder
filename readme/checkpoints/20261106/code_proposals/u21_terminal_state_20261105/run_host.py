from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='scanner-u21-audit-') as directory:
    executable=Path(directory)/'audit'
    subprocess.run(['gcc','-std=c11','-O0','-g','-Wall','-Wextra','-Werror',
        '-Wno-unused-function','-Wno-unused-variable','-Wno-comment',
        '-I'+str(root/'include'),'-I'+str(root),str(root/'audit_host.c'),'-o',str(executable)],check=True)
    result=subprocess.run([str(executable)],text=True,capture_output=True)
    (root/'host_result.txt').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr)
    result.check_returncode()
