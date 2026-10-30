"""Compile actual Scanner functions against deterministic HAL/RTOS mocks.

No target connection. Fatal loops alone are replaced by longjmp for inspection.
The model is sequential with explicit reset/ISR injection, not a real scheduler.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile

OUT = Path(__file__).resolve().parent
ROOT = Path('/home/andrey/STM32CubeIDE/workspace_1.19.0')
FW = ROOT / 'STM32F103_scanner'
FILES = [
    'App/src/app_safety.c', 'App/src/scan_uart_driver.c',
    'App/src/scan_controller.c', 'App/src/scan_ssi.c',
    'App/src/tasks/task_scan_controller.c', 'App/src/tasks/task_dispatcher.c',
    'Core/Src/main.c', 'Core/Src/stm32f1xx_it.c',
    'App/inc/app_safety.h', 'App/inc/scan_uart_driver.h',
    'App/inc/tasks/task_scan_controller.h', 'App/inc/app_config.h',
    'App/inc/tasks/task_watchdog.h', 'App/inc/scan_controller.h',
    'App/inc/scan_ssi.h', 'Core/Inc/main.h',
]
sources = {name: (FW / name).read_text() for name in FILES}


def function(text, name):
    match = re.search(r'^(?:static\s+)?(?:void|bool|uint16_t)\s+' + name
                      + r'\s*\([^)]*\)\s*\{', text, re.M)
    assert match, name
    opening = text.index('{', match.start())
    return text[match.start():block_end(text, opening)]


def block_end(text, opening):
    depth, pos = 1, opening + 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return pos


def no_includes(text):
    return re.sub(r'^\s*#include[^\n]*', '', text, flags=re.M)


parts = []
config = sources['App/inc/app_config.h']
for name in ('ParsedCanCommand_t', 'ScanResult_t'):
    parts.append(re.search(r'typedef struct \{[^}]*\}\s*' + name + ';', config).group())
parts += re.findall(r'^#define\s+(?:FLAG_\w+|SCAN_QUEUE_LEN)\s+[^\n]+', config, re.M)
parts += re.findall(r'^#define\s+\w+(?:_Pin|_GPIO_Port)\s+[^\n]+', sources['Core/Inc/main.h'], re.M)
for name in FILES[:5]:
    parts.append(no_includes(sources[name]))
dispatcher = sources['App/src/tasks/task_dispatcher.c']
for name in ('Dispatcher_ReadLe16', 'Dispatcher_PrepareReset'):
    parts.append(function(dispatcher, name))
cases = []
for name in ('REBOOT', 'FACTORY_RESET'):
    start = dispatcher.index('case CAN_CMD_SRV_' + name + ':')
    opening = dispatcher.index('{', start)
    cases.append(dispatcher[start:block_end(dispatcher, opening)])
parts.append('static void host_dispatch(ParsedCanCommand_t parsed) { switch(parsed.cmd_code) {\n'
             + '\n'.join(cases) + '\n default: assert(0); }}')
for name in ('Error_Handler',):
    body = function(sources['Core/Src/main.c'], name)
    body = re.sub(r'while\s*\(1\)\s*\{\s*\}', 'host_terminal(1);', body)
    parts.append(body)
it = sources['Core/Src/stm32f1xx_it.c']
parts.append(function(it, 'EnterSafeFaultState'))
for name in ('NMI_Handler', 'HardFault_Handler', 'MemManage_Handler',
             'BusFault_Handler', 'UsageFault_Handler'):
    body = function(it, name)
    body = re.sub(r'while\s*\(1\)\s*\{(?:\s|/\*.*?\*/)*\}',
                  'host_terminal(1);', body, flags=re.S)
    parts.append(body)
(OUT / 'extracted.inc').write_text('\n\n'.join(parts) + '\n')

with tempfile.TemporaryDirectory(prefix='scanner-reset-host-') as temp:
    binary = Path(temp) / 'host'
    # Canonical header contains the literal "*_BASE/*_LAST" in a comment.
    command = ['gcc', '-std=c11', '-O0', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-comment',
               '-I' + str(OUT), '-I' + str(FW / 'App/inc'), '-I' + str(FW / 'App/inc/tasks'),
               '-I' + str(ROOT / 'DDS-240_readme/DDS-240_eko_system'),
               str(OUT / 'host.c'), '-o', str(binary)]
    build = subprocess.run(command, text=True, capture_output=True)
    (OUT / 'host_build.log').write_text(build.stdout + build.stderr)
    if build.returncode:
        raise SystemExit(build.stderr)
    run = subprocess.run([str(binary)], text=True, capture_output=True)
    (OUT / 'host_result.txt').write_text(run.stdout + run.stderr)
    print(run.stdout + run.stderr, end='')
    evidence = {
        'source_sha256': {name: hashlib.sha256((FW / name).read_bytes()).hexdigest() for name in FILES},
        'global_config_sha256': hashlib.sha256((ROOT / 'DDS-240_readme/DDS-240_eko_system/dds240_global_config.h').read_bytes()).hexdigest(),
        'scanner_head': subprocess.check_output(['git', '-C', str(FW), 'rev-parse', 'HEAD'], text=True).strip(),
        'compiler': subprocess.check_output(['gcc', '--version'], text=True).splitlines()[0],
        'compile_command': command,
        'host_exit_code': run.returncode,
        'limits': 'Mock HAL/RTOS, explicit sequential interleavings. No real DMA, CAN bus, SSI module, scheduler, fault exception or watchdog reset. Fatal loops only replaced by longjmp.',
    }
    (OUT / 'evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    raise SystemExit(run.returncode)
