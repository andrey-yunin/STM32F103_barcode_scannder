from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='scanner-ssi-recovery-') as directory:
    executable=Path(directory)/'recovery'
    subprocess.run(['gcc','-std=c11','-O0','-g','-Wall','-Wextra','-Werror',
        '-Wno-unused-function','-Wno-unused-variable','-Wno-comment',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-I'+str(root/'include'),'-I'+str(root),str(root/'recovery_host.c'),'-o',str(executable)],check=True)
    environment=dict(os.environ);environment['ASAN_OPTIONS']='detect_leaks=0'
    result=subprocess.run([str(executable)],text=True,capture_output=True,env=environment)
    (root/'operation_host_result.txt').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr)
    result.check_returncode()
