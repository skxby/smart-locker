"""Program and observe the non-actuating S2-01 shell, with exact image guards.

Run only while MOS 12V DC input is disconnected. Requires --program and the
expected SHA-256 of the product HEX; never changes option bytes or other sectors.
"""
import argparse
import hashlib
import json
import re
import time
from datetime import datetime
from pathlib import Path

from intelhex import IntelHex
from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target

ROOT = Path(__file__).resolve().parents[4]
ENV_IMAGE = ROOT / 'build/firmware-env/hsi16/locker_env.hex'
BACKUP = Path.home() / 'Documents/locker-hardware-backups/2026-10-28/stm32f407-original-121141.bin'
BACKUP_SHA = 'e893d0f0235dac60fa12d9b884486c65adf50a9d589e5ecaa45120f0b3df9ffa'
OLD_IMAGE_SHA = '4dd05e740630de81ed4775a81715a558d68ba5b86fb094eb03a6668e406c3649'


def symbols(names, map_file):
    mapping = {}
    body = map_file.read_text(encoding='utf-8', errors='replace')
    for name in names:
        match = re.search(r'^\s*' + re.escape(name) + r'\s+(0x[0-9a-fA-F]+)\s+Data\s+', body, re.M)
        if not match:
            raise ValueError('Missing data symbol: ' + name)
        mapping[name] = int(match.group(1), 16)
    return mapping


def expected_current(original, old_image):
    image = IntelHex(str(old_image))
    sector = bytearray(b'\xff' * 0x4000)
    for address in image.addresses():
        if not 0x08000000 <= address < 0x08004000:
            raise ValueError('Old environment image exceeds sector 0')
        sector[address - 0x08000000] = image[address]
    return bytes(sector) + original[0x4000:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', action='store_true')
    parser.add_argument('--clock', choices=['hsi16', 'hse8_168'], default='hsi16')
    parser.add_argument('--target', choices=['s201', 's202'], default='s201')
    parser.add_argument('--frequency', type=int, choices=[100000, 950000], default=100000)
    parser.add_argument('--expected-image-sha256', required=True)
    parser.add_argument('--expected-current-hex', default=str(ENV_IMAGE))
    parser.add_argument('--expected-current-sha256', default=OLD_IMAGE_SHA)
    parser.add_argument('--output', type=Path,
                        help='New evidence file; existing evidence is never overwritten')
    args = parser.parse_args()
    if not args.program:
        parser.error('Explicit --program required')
    image_path = ROOT / ('build/firmware-' + args.target) / args.clock / ('locker_' + args.target + '.hex')
    map_file = image_path.with_suffix('.map')
    old_image = Path(args.expected_current_hex)
    output = args.output or Path(__file__).with_name('board-run.json' if args.clock == 'hsi16' else 'board-run-hse8_168.json')
    if output.exists():
        parser.error('Evidence already exists; select a new --output path')
    result = {'start': datetime.now().astimezone().isoformat(), 'status': 'IN_PROGRESS', 'samples': []}
    try:
        original = BACKUP.read_bytes()
        if len(original) != 1048576 or hashlib.sha256(original).hexdigest() != BACKUP_SHA:
            raise ValueError('Original backup is missing or changed')
        if hashlib.sha256(old_image.read_bytes()).hexdigest().lower() != args.expected_current_sha256.lower():
            raise ValueError('Previously flashed image is missing or changed')
        hex_sha = hashlib.sha256(image_path.read_bytes()).hexdigest()
        if hex_sha.lower() != args.expected_image_sha256.lower():
            raise ValueError('Product image SHA mismatch')
        result['image_sha256'] = hex_sha
        image = IntelHex(str(image_path))
        segments = image.segments()
        if not segments or any(a < 0x08000000 or b > 0x08004000 for a, b in segments):
            raise ValueError('Product image exceeds Flash sector 0')
        address = symbols(['s201_fault', 's201_safety_checks', 'SystemCoreClock', 'uwTick', 'xTickCount', 'tick_high'], map_file)
        options = {'target_override': 'stm32f407vgtx',
                   'pack': 'E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0',
                   'frequency': args.frequency, 'connect_mode': 'attach',
                   'reset_type': 'sysresetreq', 'auto_unlock': False,
                   'resume_on_disconnect': False, 'hide_programming_progress': True,
                   'pack.debug_sequences.enable': False}
        session = ConnectHelper.session_with_chosen_probe(
            blocking=False, unique_id='000000000000000000000000', options=options)
        if session is None:
            raise RuntimeError('Verified ST-LINK unavailable')
        with session:
            target = session.target
            def prepare():
                target.reset(Target.ResetType.SYSRESETREQ)
                time.sleep(0.1)
                target.halt()
                target.flush()
                if target.get_state() != Target.State.HALTED:
                    raise RuntimeError('Unable to halt target')
                target.write32(0x40023C00, (target.read32(0x40023C00) & ~7) | 5)
                target.write32(0x40023800, target.read32(0x40023800) | 1)
                target.flush()
                if not target.read32(0x40023800) & 2:
                    raise RuntimeError('HSI failed to start')
                target.write32(0x40023808, target.read32(0x40023808) & ~3)
                target.write32(0xE000E010, 0)
                target.write_core_register('primask', 1)
                target.write_core_register('xpsr', 0x01000000)
                target.flush()
            prepare()
            if target.read16(0x1FFF7A22) != 1024:
                raise RuntimeError('Unexpected Flash capacity')
            current = bytes(target.read_memory_block8(0x08000000, len(original)))
            if current != expected_current(original, old_image):
                raise RuntimeError('Current Flash differs from the expected previous image and backup')
            result['preflash_state'] = 'exact_previous_image_and_backup_tail'
            flash = target.memory_map.get_boot_memory().flash
            flash.set_flash_algo_debug(True)
            flash.init(flash.Operation.ERASE, reset=False)
            print('START_PROGRAM: ' + args.target + ' ' + args.clock + '; sector 0 only', flush=True)
            flash.erase_sector(0x08000000)
            flash.uninit()
            prepare()
            flash.init(flash.Operation.PROGRAM, reset=False)
            payload = bytes(image.tobinarray(start=0x08000000, end=image.maxaddr()))
            for offset in range(0, len(payload), 1024):
                flash.program_page(0x08000000 + offset, payload[offset:offset + 1024].ljust(1024, b'\xff'))
            flash.uninit()
            for start, end in segments:
                if bytes(target.read_memory_block8(start, end - start)) != bytes(image.tobinarray(start=start, end=end - 1)):
                    raise RuntimeError('Program readback mismatch')
            if bytes(target.read_memory_block8(0x08004000, len(original) - 0x4000)) != original[0x4000:]:
                raise RuntimeError('Flash outside sector 0 changed')
            result['readback_verified'] = True
            target.reset(Target.ResetType.SYSRESETREQ)
            time.sleep(0.1)
            target.halt()
            pc = target.read_core_register('pc')
            result['pc_after_reset'] = hex(pc)
            if not 0x08000000 <= pc < 0x08004000:
                raise RuntimeError('Reset did not select product Flash')
            target.resume()
            start = time.monotonic()
            for i in range(6):
                if i: time.sleep(1.2)
                sample = {'elapsed_s': round(time.monotonic() - start, 3),
                          'state': target.get_state().name,
                          **{name: target.read32(addr) for name, addr in address.items()},
                          'gpiob_moder': target.read32(0x40020400),
                          'gpiob_odr': target.read32(0x40020414)}
                result['samples'].append(sample)
                print(json.dumps(sample), flush=True)
            samples = result['samples']
            expected_clock = 16000000 if args.clock == 'hsi16' else 168000000
            if any(s['state'] != 'RUNNING' or s['s201_fault'] != 0 or s['SystemCoreClock'] != expected_clock for s in samples):
                raise RuntimeError('Product shell did not run safely')
            if any(s['s201_safety_checks'] != 15 for s in samples):
                raise RuntimeError('Unverified profile or pulse rejection check failed')
            if any((s['gpiob_moder'] >> 12) & 3 or (s['gpiob_odr'] >> 6) & 1 for s in samples):
                raise RuntimeError('PB6 unexpectedly configured as output or driven high')
            elapsed = samples[-1]['elapsed_s'] - samples[0]['elapsed_s']
            result['hal_ticks_per_second'] = (samples[-1]['uwTick'] - samples[0]['uwTick']) / elapsed
            result['rtos_ticks_per_second'] = (samples[-1]['xTickCount'] - samples[0]['xTickCount']) / elapsed
            if not (800 <= result['hal_ticks_per_second'] <= 1200 and
                    800 <= result['rtos_ticks_per_second'] <= 1200):
                raise RuntimeError('HAL or RTOS tick rate outside expected range')
            result['status'] = 'PASS'
    except Exception as error:
        result['status'] = 'FAIL'
        result['error'] = repr(error)
        raise
    finally:
        result['end'] = datetime.now().astimezone().isoformat()
        output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('RESULT ' + result['status'], flush=True)


if __name__ == '__main__':
    main()
