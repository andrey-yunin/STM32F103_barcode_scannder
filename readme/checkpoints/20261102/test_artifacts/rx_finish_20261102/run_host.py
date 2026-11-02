"""Replay archived actual-code HAL/RTOS model; never connects to the target."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='scanner-rx-finish-') as temporary:
    executable = Path(temporary) / 'host'
    command = [
        'gcc', '-std=c11', '-O0', '-g', '-Wall', '-Wextra', '-Werror',
        '-Wno-unused-function', '-Wno-unused-variable', '-Wno-comment',
        '-I' + str(root / 'include'), str(root / 'host.c'), '-o', str(executable),
    ]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
