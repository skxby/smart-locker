"""Build only the non-actuating S2-04 SWD target with a supplied GNU Arm toolchain.

No downloader, install, flashing, dependency mutation or global PATH changes.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

from build_firmware import ROOT, FW, SOURCES, INCLUDES


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--toolchain-root', required=True,
                        help='Installed GNU Arm toolchain directory containing bin/')
    parser.add_argument('--target', choices=['s204', 's205', 's205-cutoff'], default='s204')
    parser.add_argument('--clock', choices=['hsi16', 'hse8_168'], default='hsi16')
    parser.add_argument('--bench-unmonitored-supply', action='store_true',
                        help='Bench only: let physical() pass without supply_valid (PC0 not wired). '
                             'Never use for a product image; supply-fault cases stay NOT_RUN.')
    parser.add_argument('--bench-real-lock', action='store_true',
                        help='Bench only: define BOARD_LOCK_QUALIFIED so PB6/TIM4 may drive a real '
                             'solenoid. Without it board_lock_apply() fails closed and no pulse happens.')
    args = parser.parse_args()
    toolroot = Path(args.toolchain_root).resolve()
    tools = {name: toolroot / 'bin' / ('arm-none-eabi-' + name + '.exe')
             for name in ['gcc', 'objcopy', 'size', 'nm', 'readelf']}
    for tool in tools.values():
        if not tool.is_file():
            raise SystemExit('Missing installed tool: ' + str(tool))
    lock = json.loads((ROOT / 'deployment/firmware/dependencies.lock.json').read_text(encoding='utf-8'))
    for relative, expected in lock['files'].items():
        if hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() != expected:
            raise SystemExit('Dependency hash changed: ' + relative)
    supplement_path = ROOT / 'deployment/firmware/gnu-supplement.lock.json'
    supplement = json.loads(supplement_path.read_text(encoding='utf-8'))
    for entry in supplement['files']:
        if hashlib.sha256((ROOT / entry['path']).read_bytes()).hexdigest() != entry['sha256']:
            raise SystemExit('GNU supplement hash changed: ' + entry['path'])
    output = ROOT / ('build/firmware-' + args.target + '-gnu') / args.clock
    output.mkdir(parents=True, exist_ok=True)
    log = []

    def run(command):
        result = subprocess.run([str(value) for value in command], cwd=FW, capture_output=True)
        content = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        log.append('$ ' + ' '.join(str(value) for value in command) + '\n' + content)
        (output / 'build.log').write_text('\n'.join(log), encoding='utf-8')
        if result.returncode:
            print(content)
            raise SystemExit(result.returncode)
        return content

    version = run([tools['gcc'], '--version']).splitlines()[0]
    base = ['-mcpu=cortex-m4', '-mthumb', '-mfpu=fpv4-sp-d16', '-mfloat-abi=hard']
    flags = [*base, '-std=c99', '-Os', '-g3', '-ffunction-sections', '-fdata-sections',
             '-fno-common', '-Wall', '-Wextra', '--specs=nano.specs']
    defines = ['STM32F407xx', 'USE_HAL_DRIVER', 'LOCKER_S202', 'LFS_NO_MALLOC',
               'LFS_NO_ASSERT', 'LFS_NO_DEBUG', 'LFS_NO_WARN', 'LFS_NO_ERROR']
    if args.clock == 'hse8_168':
        defines.append('ENV_HSE8_168')
    if args.target == 's205':
        defines += ['LOCKER_S203', 'LOCKER_S205', 'BOARD_UART_QUALIFIED=1', 'BOARD_VERIFIED=1']
        if args.bench_unmonitored_supply:
            defines += ['LOCKER_BENCH_SUPPLY_UNMONITORED=1']
        if args.bench_real_lock:
            defines += ['BOARD_LOCK_QUALIFIED=1']
    if args.target == 's205-cutoff':
        defines += ['BOARD_LOCK_QUALIFIED=1']
    includes = [p for p in INCLUDES if 'portable/RVDS/' not in p]
    includes += ['third_party/freertos/portable/GCC/ARM_CM4F', 'hal/cmsis-gcc',
                 'board', 'drivers', 'drivers/flash', 'journal', '../contracts/include']
    if args.target.startswith('s205'):
        includes += ['control', 'tasks', 'transport', '../host/device/include']
    flags += ['-D' + name for name in defines] + ['-I' + path for path in includes]
    sources = ['journal/validation_main.c', 'board/board.c', 'hal/timebase_tim6.c', *SOURCES[2:]]
    sources = [p.replace('portable/RVDS/ARM_CM4F/', 'portable/GCC/ARM_CM4F/') for p in sources]
    sources += ['board/peripherals.c', 'drivers/inputs.c', 'drivers/rc522.c',
                'journal/board_validation.c', 'journal/journal.c', 'drivers/flash/w25q128.c']
    if args.target == 's205':
        sources[0] = 'app/main.c'
        sources.remove('journal/board_validation.c')
        sources += ['board/lock_output.c', 'control/control.c', 'tasks/assembly.c',
                    'tasks/communication.c', 'tasks/health.c', 'transport/transport.c',
                    '../host/device/src/wire.c', '../host/device/src/codec.c']
    if args.target == 's205-cutoff':
        sources[0] = 'control/cutoff_validation.c'
        sources.remove('journal/board_validation.c')
        sources += ['board/lock_output.c']
    objects = []
    for index, source in enumerate(sources):
        obj = output / (f'{index:02d}_' + Path(source).stem + '.o')
        relative = os.path.relpath(obj, FW)
        run([tools['gcc'], *flags, '-c', source, '-o', relative])
        objects.append(relative)
    startup = os.path.relpath(output / 'startup_gnu.o', FW)
    run([tools['gcc'], *base, '-g3', '-x', 'assembler-with-cpp', '-c',
         'journal/startup_gnu.S', '-o', startup])
    image = output / ('locker_' + args.target + '.elf')
    mapfile = output / ('locker_' + args.target + '.map')
    run([tools['gcc'], *base, '-nostartfiles', '--specs=nano.specs', '--specs=nosys.specs',
         '-T', 'keil/stm32f407vg.ld', '-Wl,--gc-sections', '-Wl,--print-memory-usage',
         '-Wl,-Map,' + os.path.relpath(mapfile, FW),
         startup, *objects, '-Wl,--start-group', '-lc', '-lm', '-lnosys', '-Wl,--end-group',
         '-o', os.path.relpath(image, FW)])
    run([tools['objcopy'], '-O', 'ihex', os.path.relpath(image, FW),
         os.path.relpath(output / ('locker_' + args.target + '.hex'), FW)])
    run([tools['objcopy'], '-O', 'binary', os.path.relpath(image, FW),
         os.path.relpath(output / ('locker_' + args.target + '.bin'), FW)])
    sizes = run([tools['size'], os.path.relpath(image, FW)])
    symbols = run([tools['nm'], '-n', '-S', os.path.relpath(image, FW)])
    (output / 'symbols.txt').write_text(symbols, encoding='utf-8')
    sections = run([tools['readelf'], '-S', '-W', os.path.relpath(image, FW)])
    (output / 'sections.txt').write_text(sections, encoding='utf-8')
    match = re.search(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+', sizes, re.MULTILINE)
    memory = {}
    if match:
        text_bytes, data_bytes, bss_bytes = map(int, match.groups())
        memory = {'rom_bytes': text_bytes + data_bytes, 'ram_bytes': data_bytes + bss_bytes}
    result = {'status': 'PASS', 'application': args.target, 'target': 'STM32F407VGTx',
              'clock_profile': args.clock, 'compiler': version, 'toolchain_root': str(toolroot),
              'translation_units': len(sources) + 1, 'dependencies_hashed': len(lock['files']),
              'supplement_files_hashed': len(supplement['files']), 'memory': memory,
              'image_sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
              'compile_link_only': True, 'hardware_tested': False,
              'uart_enabled': args.target == 's205', 'lock_qualified': False,
              'supply_monitor_bypassed': bool(args.bench_unmonitored_supply and args.target == 's205'),
              'real_lock_drive': bool(args.bench_real_lock and args.target == 's205'),
              'isolated_pulse_bench': args.target == 's205-cutoff',
              'cabinet_business_implemented': False}
    (output / 'summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
