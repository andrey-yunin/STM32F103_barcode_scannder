from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
workspace = next(path for path in root.parents if (path / 'STM32F103_scanner').is_dir())
project = workspace / 'STM32F103_scanner'

# Compile the actual codec with the proposed receive helpers; no firmware edits.
with tempfile.TemporaryDirectory(prefix='scanner-ssi-receive-') as directory:
    executable = Path(directory) / 'receive_host'
    subprocess.run([
        'gcc', '-std=c11', '-O0', '-g', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I' + str(project / 'App/inc'), '-I' + str(root),
        str(root / 'receive_host.c'), str(project / 'App/src/scan_ssi.c'),
        '-o', str(executable),
    ], check=True)
    import os
    task_environment = dict(os.environ)
    task_environment['ASAN_OPTIONS'] = 'detect_leaks=0'
    result = subprocess.run([str(executable)], capture_output=True, text=True, env=task_environment)
    (root / 'host_result.txt').write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr)
    result.check_returncode()
