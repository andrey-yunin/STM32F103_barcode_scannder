from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parent
workspace=next(p for p in root.parents if (p/'STM32F103_scanner').is_dir())
project=workspace/'STM32F103_scanner'
with tempfile.TemporaryDirectory(prefix='scanner-codec-installed-') as directory:
    for name in ['legacy_codec','status_host']:
        exe=Path(directory)/name
        subprocess.run(['gcc','-std=c11','-O0','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(project/'App/inc'),str(root/(name+'.c')),str(project/'App/src/scan_ssi.c'),'-o',str(exe)],check=True)
        environment=dict(os.environ);environment['ASAN_OPTIONS']='detect_leaks=0'
        r=subprocess.run([str(exe)],text=True,capture_output=True,env=environment)
        (root/(name+'_result.txt')).write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
