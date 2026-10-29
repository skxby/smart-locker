"""Guarded S2-03 non-actuating UART image programming and SWD observation.

Requires explicit --program, new/old HEX hashes and a new evidence path. Only
Flash sectors 0/1 are erased/programmed; option bytes are never changed.
Do not run while MOS 12V or a real lock is connected.
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
FLASH_BASE = 0x08000000
SECTOR_SIZE = 0x4000
WINDOW_SIZE = 0x8000
BACKUP = Path.home() / 'Documents/locker-hardware-backups/2026-10-28/stm32f407-original-121141.bin'
BACKUP_SHA = 'e893d0f0235dac60fa12d9b884486c65adf50a9d589e5ecaa45120f0b3df9ffa'
S202_HEX_SHA = '25a23854ed0f7c157b3d763dd45356893e90aa453f6d4701de07e1bdfb66a7ee'


def root_path(value):
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def symbols(names, map_file):
    body = map_file.read_text(encoding='utf-8', errors='replace')
    result = {}
    for name in names:
        match = re.search(r'^\s*' + re.escape(name) + r'\s+(0x[0-9a-fA-F]+)\s+Data\s+', body, re.M)
        if not match:
            raise ValueError('Missing data symbol: ' + name)
        address = int(match.group(1), 16)
        if not 0x20000000 <= address < 0x20020000:
            raise ValueError('Data symbol outside main SRAM: ' + name)
        result[name] = address
    if result['s203_diag'] + 24 * 4 > 0x20020000:
        raise ValueError('Diagnostic array exceeds main SRAM')
    return result


def load_hex(path, expected_sha, maximum):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest.lower() != expected_sha.lower():
        raise ValueError('HEX SHA mismatch: ' + str(path))
    image = IntelHex(str(path))
    segments = image.segments()
    if not segments or any(a < FLASH_BASE or b > FLASH_BASE + maximum for a, b in segments):
        raise ValueError('HEX exceeds allowed Flash window: ' + str(path))
    occupied = set(image.addresses())
    if not all(FLASH_BASE + i in occupied for i in range(8)):
        raise ValueError('HEX has no complete initial stack/reset vector')
    return image, digest


def previous_flash(original, old_image):
    # The accepted S202 image used sector 0 only, FF-filled gaps/tail there.
    sector = bytearray(b'\xff' * SECTOR_SIZE)
    for address in old_image.addresses():
        sector[address - FLASH_BASE] = old_image[address]
    return bytes(sector) + original[SECTOR_SIZE:]


def new_window(original, image):
    # FF-fill the new image extent, preserving original bytes beyond its end,
    # including the unoverwritten tail inside sector 1 that must be reprogrammed.
    extent = image.maxaddr() - FLASH_BASE + 1
    window = bytearray(b'\xff' * extent + original[extent:WINDOW_SIZE])
    for address in image.addresses():
        window[address - FLASH_BASE] = image[address]
    return bytes(window), extent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', action='store_true')
    parser.add_argument('--image', required=True, help='Absolute or repository-relative new S203 HEX')
    parser.add_argument('--expected-image-sha256', required=True)
    parser.add_argument('--expected-current-hex', required=True)
    parser.add_argument('--expected-current-sha256', required=True)
    parser.add_argument('--clock', choices=['hsi16', 'hse8_168'], default='hse8_168')
    parser.add_argument('--frequency', type=int, choices=[100000, 950000], default=100000)
    parser.add_argument('--output', required=True, help='New absolute or repository-relative evidence JSON')
    args = parser.parse_args()
    if not args.program:
        parser.error('Explicit --program required')
    for value in [args.expected_image_sha256, args.expected_current_sha256]:
        if not re.fullmatch(r'[0-9a-fA-F]{64}', value):
            parser.error('Expected SHA-256 must be 64 hexadecimal digits')
    if args.expected_current_sha256.lower() != S202_HEX_SHA:
        parser.error('This transition requires the accepted S202 HSE image SHA')
    image_path = root_path(args.image)
    old_image_path = root_path(args.expected_current_hex)
    output = root_path(args.output)
    if output.exists():
        parser.error('Evidence already exists; select a new --output path')
    if not output.parent.is_dir():
        parser.error('Evidence parent directory must already exist')
    # Exclusive reservation prevents a race from overwriting previous evidence.
    with output.open('x', encoding='utf-8') as evidence:
        result = {'start': datetime.now().astimezone().isoformat(), 'status': 'IN_PROGRESS',
                  'image': str(image_path), 'previous_image': str(old_image_path),
                  'backup': str(BACKUP), 'samples': [],
                  'stack_high_water': 'NOT_MEASURED: runtime query is not configured; no RAM injection',
                  'programmed_sectors': ['0x08000000..0x08003fff', '0x08004000..0x08007fff']}
        try:
            original = BACKUP.read_bytes()
            if len(original) != 1048576 or hashlib.sha256(original).hexdigest() != BACKUP_SHA:
                raise ValueError('Original 1MiB backup is missing or changed')
            old_image, old_sha = load_hex(old_image_path, args.expected_current_sha256, SECTOR_SIZE)
            image, hex_sha = load_hex(image_path, args.expected_image_sha256, WINDOW_SIZE)
            expected_before = previous_flash(original, old_image)
            window, extent = new_window(original, image)
            reset_vector = int.from_bytes(window[4:8], 'little')
            if reset_vector & 1 == 0 or not FLASH_BASE <= (reset_vector & ~1) < FLASH_BASE + extent:
                raise ValueError('New reset vector is outside the new image')
            address = symbols(['s201_fault', 's201_safety_checks', 'SystemCoreClock',
                               'uwTick', 'xTickCount', 's203_diag'], image_path.with_suffix('.map'))
            result.update(image_sha256=hex_sha, previous_image_sha256=old_sha,
                          original_backup_sha256=BACKUP_SHA, image_extent_bytes=extent,
                          programmed_window_sha256=hashlib.sha256(window).hexdigest(),
                          image_segments=[[hex(a), hex(b)] for a, b in image.segments()])
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
                    # Same explicit clock/core setup as the retained S201 tool.
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
                    target.flush()
                    time.sleep(0.02)
                    if target.read32(0x40023808) & 12:
                        raise RuntimeError('CPU did not switch to HSI')
                    target.write32(0xE000E010, 0)
                    target.write_core_register('primask', 1)
                    target.write_core_register('xpsr', 0x01000000)
                    target.flush()

                prepare()
                if target.read16(0x1FFF7A22) != 1024:
                    raise RuntimeError('Unexpected Flash capacity')
                current = bytes(target.read_memory_block8(FLASH_BASE, len(original)))
                if current != expected_before:
                    raise RuntimeError('Current 1MiB Flash differs from S202 sector 0 + exact original backup tail')
                result['preflash_state'] = 'exact_s202_sector0_ff_padding_and_original_tail'
                result['preflash_full_sha256'] = hashlib.sha256(current).hexdigest()
                flash = target.memory_map.get_boot_memory().flash
                flash.set_flash_algo_debug(True)
                flash.init(flash.Operation.ERASE, reset=False)
                try:
                    print('START_PROGRAM: S203; sectors 0 and 1 only', flush=True)
                    flash.erase_sector(FLASH_BASE)
                    flash.erase_sector(FLASH_BASE + SECTOR_SIZE)
                finally:
                    flash.uninit()
                result['sectors_0_1_erased'] = True
                prepare()
                flash.init(flash.Operation.PROGRAM, reset=False)
                try:
                    for offset in range(0, WINDOW_SIZE, 1024):
                        flash.program_page(FLASH_BASE + offset, window[offset:offset + 1024])
                finally:
                    flash.uninit()
                if bytes(target.read_memory_block8(FLASH_BASE, WINDOW_SIZE)) != window:
                    raise RuntimeError('Full 32KiB programmed window readback mismatch')
                result['full_two_sectors_readback_verified'] = True
                if bytes(target.read_memory_block8(FLASH_BASE + WINDOW_SIZE,
                                                  len(original) - WINDOW_SIZE)) != original[WINDOW_SIZE:]:
                    raise RuntimeError('Flash after sector 1 changed')
                result['outside_two_sectors_unchanged'] = True
                result['preserved_in_window_tail_verified'] = True
                result['readback_verified'] = True
                target.reset(Target.ResetType.SYSRESETREQ)
                time.sleep(0.1)
                target.halt()
                target.flush()
                pc = target.read_core_register('pc')
                result['pc_after_reset'] = hex(pc)
                if not FLASH_BASE <= pc < FLASH_BASE + extent:
                    raise RuntimeError('Reset did not select S203 image')
                target.resume()
                started = time.monotonic()
                paused_total = 0.0
                for i in range(6):
                    if i:
                        time.sleep(1.2)
                    state = target.get_state().name
                    target.halt()
                    halted_at = time.monotonic()
                    try:
                        sample = {'elapsed_s': round(halted_at - started, 3),
                                  'active_elapsed_s': round(halted_at - started - paused_total, 3),
                                  'state_before_snapshot': state,
                                  **{name: target.read32(addr) for name, addr in address.items()
                                     if name != 's203_diag'},
                                  's203_diag': list(target.read_memory_block32(address['s203_diag'], 24)),
                                  'gpiob_moder': target.read32(0x40020400),
                                  'gpiob_odr': target.read32(0x40020414),
                                  'dma1_stream5_ndtr': target.read32(0x4002608C),
                                  'dma1_stream6_ndtr': target.read32(0x400260A4)}
                    finally:
                        target.resume()
                        paused_total += time.monotonic() - halted_at
                    result['samples'].append(sample)
                    print(json.dumps(sample), flush=True)
                samples = result['samples']
                expected_clock = 16000000 if args.clock == 'hsi16' else 168000000
                if any(s['state_before_snapshot'] != 'RUNNING' or s['s201_fault'] != 0 or
                       s['s201_safety_checks'] != 15 or s['SystemCoreClock'] != expected_clock for s in samples):
                    raise RuntimeError('S203 did not run with expected clock/safety checks')
                if any(((s['gpiob_moder'] >> 12) & 3) or ((s['gpiob_odr'] >> 6) & 1) for s in samples):
                    raise RuntimeError('PB6 changed from input-low safety state')
                if any(s['s203_diag'][0] != 0x53323033 or s['s203_diag'][1] != 1 for s in samples):
                    raise RuntimeError('S203 qualified UART task did not initialize successfully')
                if any(not 0 <= s['dma1_stream5_ndtr'] <= 2048 for s in samples):
                    raise RuntimeError('RX DMA NDTR outside 2048-byte ring')
                if any(b['s203_diag'][2] <= a['s203_diag'][2] for a, b in zip(samples, samples[1:])):
                    raise RuntimeError('Communication task made no progress')
                elapsed = samples[-1]['active_elapsed_s'] - samples[0]['active_elapsed_s']
                result['sampling'] = 'brief halt/read/resume; halt time removed from tick-rate denominator'
                result['hal_ticks_per_second'] = ((samples[-1]['uwTick'] - samples[0]['uwTick']) & 0xffffffff) / elapsed
                result['rtos_ticks_per_second'] = ((samples[-1]['xTickCount'] - samples[0]['xTickCount']) & 0xffffffff) / elapsed
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
            evidence.write(json.dumps(result, indent=2) + '\n')
            evidence.flush()
            print('RESULT ' + result['status'], flush=True)


if __name__ == '__main__':
    main()
