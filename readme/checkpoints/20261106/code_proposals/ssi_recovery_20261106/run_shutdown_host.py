from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='scanner-shutdown-regression-') as directory:
    exe=Path(directory)/'shutdown'
    subprocess.run(['gcc','-std=c11','-O0','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-comment','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'include'),'-I'+str(root),str(root/'shutdown_host.c'),'-o',str(exe)],check=True)
    environment=dict(os.environ);environment['ASAN_OPTIONS']='detect_leaks=0'
    r=subprocess.run([str(exe)],text=True,capture_output=True,env=environment)
    (root/'shutdown_host_result.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
