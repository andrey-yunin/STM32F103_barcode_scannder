from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='scanner-flash-uart-') as directory:
    for name in ['flash_host','host','regression']:
        executable=Path(directory)/name
        subprocess.run(['gcc','-std=c11','-O0','-g','-Wall','-Wextra','-Werror',
            '-Wno-unused-function','-Wno-unused-variable','-Wno-comment',
            '-I'+str(root/'include'),'-I'+str(root),str(root/(name+'.c')),'-o',str(executable)],check=True)
        result=subprocess.run([str(executable)],capture_output=True,text=True)
        (root/(name+'_result.txt')).write_text(result.stdout+result.stderr)
        print(name+': '+('PASS' if result.returncode==0 else 'FAIL'),flush=True)
        if result.returncode: print(result.stdout+result.stderr,flush=True)
        result.check_returncode()
