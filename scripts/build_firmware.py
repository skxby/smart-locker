"""Compile/link the non-actuating STM32 environment target using installed ARMCC5."""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / 'firmware'
HAL_MODULES = ['hal','hal_cortex','hal_rcc','hal_rcc_ex','hal_flash','hal_flash_ex','hal_pwr','hal_pwr_ex','hal_gpio','hal_dma','hal_adc','hal_adc_ex','hal_spi','hal_uart','hal_tim','hal_tim_ex','hal_iwdg']
SOURCES = ['env/main.c', 'env/hal_timebase_tim6.c', 'third_party/stm32f4/cmsis/system_stm32f4xx.c']
SOURCES += ['third_party/stm32f4/hal/Src/stm32f4xx_' + x + '.c' for x in HAL_MODULES]
SOURCES += ['third_party/freertos/' + x + '.c' for x in ['tasks','queue','list','timers','event_groups','stream_buffer','croutine']]
SOURCES += ['third_party/freertos/portable/RVDS/ARM_CM4F/port.c','third_party/freertos/portable/MemMang/heap_4.c','third_party/littlefs/lfs.c','third_party/littlefs/lfs_util.c']
INCLUDES = ['config','third_party/stm32f4/cmsis','third_party/stm32f4/hal/Inc','third_party/freertos/include','third_party/freertos/portable/RVDS/ARM_CM4F','third_party/littlefs']
DEFINES = ['STM32F407xx','USE_HAL_DRIVER','__MICROLIB','LFS_NO_MALLOC','LFS_NO_ASSERT','LFS_NO_DEBUG','LFS_NO_WARN','LFS_NO_ERROR']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--keil-root', default='E:/Keil5')
    ap.add_argument('--clock', choices=['hsi16','hse8_168'], default='hsi16')
    ap.add_argument('--target', choices=['environment','s201','s202'], default='environment')
    ap.add_argument('--check-adc-build', action='store_true', help='Compile-only S202 ADC branch in an isolated output directory')
    args = ap.parse_args()
    if args.check_adc_build and args.target != 's202':
        ap.error('--check-adc-build requires --target s202')
    tools = Path(args.keil_root) / 'ARM/ARMCC/bin'
    output = ROOT / ('build/firmware-env' if args.target == 'environment' else 'build/firmware-' + args.target) / args.clock
    if args.check_adc_build: output = ROOT / 'build/firmware-s202-adc-compile-only' / args.clock
    output.mkdir(parents=True, exist_ok=True)
    log = []

    def run(command):
        result = subprocess.run([str(x) for x in command], cwd=FW, capture_output=True)
        content = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        log.append('$ ' + ' '.join(str(x) for x in command) + '\n' + content)
        (output / 'build.log').write_text('\n'.join(log), encoding='utf-8')
        if result.returncode:
            print(content)
            raise SystemExit(result.returncode)
        return content

    for name in ['armcc.exe','armasm.exe','armlink.exe','fromelf.exe']:
        if not (tools / name).is_file():
            raise SystemExit('Missing installed tool: ' + str(tools / name))
    lock = json.loads((ROOT / 'deployment/firmware/dependencies.lock.json').read_text(encoding='utf-8'))
    for relative, expected in lock['files'].items():
        if hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() != expected:
            raise SystemExit('Dependency hash changed: ' + relative)
    version = run([tools / 'armcc.exe','--vsn'])
    flags = ['--cpu','Cortex-M4.fp','--fpu','FPv4-SP','--c99','--split_sections','--library_type=microlib','-O2','-g']
    defines = DEFINES + (['ENV_HSE8_168'] if args.clock == 'hse8_168' else [])
    if args.target == 's202': defines += ['LOCKER_S202']
    if args.check_adc_build: defines += ['BOARD_ADC_QUALIFIED=1']
    flags += ['-D' + item for item in defines] + ['-I' + item for item in (INCLUDES + ['board','drivers','../contracts/include'])]
    sources = SOURCES if args.target == 'environment' else ['app/main.c','board/board.c','hal/timebase_tim6.c', *SOURCES[2:]]
    if args.target == 's202': sources += ['board/peripherals.c','drivers/inputs.c','drivers/rc522.c']
    objects = []
    for index, source in enumerate(sources):
        obj = output / (f'{index:02d}_' + Path(source).stem + '.o')
        relative = os.path.relpath(obj, FW)
        run([tools / 'armcc.exe', *flags, '-c', source, '-o', relative])
        objects.append(relative)
    startup = os.path.relpath(output / 'startup_stm32f407xx.o', FW)
    run([tools / 'armasm.exe','--cpu','Cortex-M4.fp','--fpu','FPv4-SP','--pd','__MICROLIB SETA 1','third_party/stm32f4/cmsis/startup_stm32f407xx.s','-o',startup])
    image_name = 'locker_env' if args.target == 'environment' else 'locker_' + args.target
    axf = os.path.relpath(output / (image_name + '.axf'), FW)
    mapfile = os.path.relpath(output / (image_name + '.map'), FW)
    link_output = run([tools / 'armlink.exe','--cpu','Cortex-M4.fp','--library_type=microlib','--strict','--scatter','keil/stm32f407vg.sct','--map','--symbols','--info=sizes','--list',mapfile,'--output',axf,startup,*objects])
    run([tools / 'fromelf.exe','--i32combined','--output',os.path.relpath(output / (image_name + '.hex'), FW),axf])
    map_text = (output / (image_name + '.map')).read_text(encoding='utf-8', errors='replace')
    sizes = {label: int(match.group(1)) for label, pattern in [('rom_bytes',r'Total ROM Size.*?\s(\d+)\s*\('),('ram_bytes',r'Total RW\s+Size.*?\s(\d+)\s*\(')] if (match := re.search(pattern, map_text))}
    summary = {'status':'PASS','target':'STM32F407VGTx','application':args.target,'clock_profile':args.clock,'compiler':version.strip(),'translation_units':len(sources)+1,'dependencies_hashed':len(lock['files']),'image_sha256':hashlib.sha256((output/(image_name+'.axf')).read_bytes()).hexdigest(),'compile_link_only':True,'hardware_tested':False,'cabinet_business_implemented':False,'memory':sizes,'size_output':link_output.strip()}
    (output / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(summary, ensure_ascii=False))


if __name__ == '__main__':
    main()
