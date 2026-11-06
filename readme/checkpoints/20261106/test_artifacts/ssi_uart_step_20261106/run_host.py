from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='scanner-uart-boundary-') as directory:
    for name in ['boundary_host','host','regression']:
        executable=Path(directory)/name
        subprocess.run(['gcc','-std=c11','-O0','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-comment','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'include'),'-I'+str(root),str(root/(name+'.c')),'-o',str(executable)],check=True)
        environment=dict(os.environ);environment['ASAN_OPTIONS']='detect_leaks=0'
        r=subprocess.run([str(executable)],text=True,capture_output=True,env=environment)
        (root/(name+'_result.txt')).write_text(r.stdout+r.stderr)
        print(name+': '+('PASS' if not r.returncode else 'FAIL'))
        if r.returncode:print(r.stdout+r.stderr)
        r.check_returncode()
