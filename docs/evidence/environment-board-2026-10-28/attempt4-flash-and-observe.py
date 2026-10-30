"""Reproduce the explicitly authorized Env_hsi16 board test; never auto-unlock."""
import argparse
import hashlib
import json
import logging
import time
from datetime import datetime
from pathlib import Path

from intelhex import IntelHex
from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target
from pyocd.flash.file_programmer import FileProgrammer

ROOT = Path(__file__).resolve().parents[3]
IMAGE = ROOT / 'build/firmware-env/hsi16/locker_env.hex'
PACK = Path('E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0')
BACKUP = Path.home() / 'Documents/locker-hardware-backups/2026-10-28/stm32f407-original-121141.bin'
IMAGE_SHA = '4997d5d2b094b333f8ce685ad19698c72a838141779ff6f2870e2a957cbc8f46'
BACKUP_SHA = 'e893d0f0235dac60fa12d9b884486c65adf50a9d589e5ecaa45120f0b3df9ffa'
OUTPUT = Path(__file__).with_name('board-run.json')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', action='store_true', help='Explicitly authorize sector-0 programming')
    parser.add_argument('--continue-erased-sector0', action='store_true',
                        help='Continue only if sector 0 is all FF and the rest matches the original backup')
    args = parser.parse_args()
    if not args.program:
        parser.error('Requires --program and human authorization to overwrite the original program')
    result = {'start': datetime.now().astimezone().isoformat(), 'status': 'IN_PROGRESS',
              'image': str(IMAGE), 'backup': str(BACKUP), 'samples': []}
    try:
        original = BACKUP.read_bytes()
        assert len(original) == 1048576 and hashlib.sha256(original).hexdigest() == BACKUP_SHA
        assert hashlib.sha256(IMAGE.read_bytes()).hexdigest() == IMAGE_SHA
        image = IntelHex(str(IMAGE))
        segments = image.segments()
        assert segments and all(0x08000000 <= a < b <= 0x08004000 for a, b in segments)
        result['image_segments'] = [[hex(a), hex(b)] for a, b in segments]
        options = {'target_override': 'stm32f407vgtx', 'pack': str(PACK),
                   'frequency': 950000, 'connect_mode': 'attach', 'reset_type': 'sysresetreq',
                   'auto_unlock': False, 'resume_on_disconnect': False,
                   'hide_programming_progress': True, 'pack.debug_sequences.enable': False}
        session = ConnectHelper.session_with_chosen_probe(
            blocking=False, unique_id='000000000000000000000000', options=options)
        if session is None:
            raise RuntimeError('The previously verified ST-LINK was not available')
        with session:
            target = session.target
            target.halt()
            target.flush()
            assert target.get_state() == Target.State.HALTED
            # FLM expects a reset-like HSI clock. A previous application may have
            # configured PLL and SysTick before reset catch failed on this probe.
            target.write32(0x40023C00, (target.read32(0x40023C00) & ~7) | 5)
            target.write32(0x40023800, target.read32(0x40023800) | 1)
            target.flush()
            assert target.read32(0x40023800) & 2
            target.write32(0x40023808, target.read32(0x40023808) & ~3)
            target.flush()
            time.sleep(0.02)
            assert target.read32(0x40023808) & 12 == 0
            target.write32(0xE000E010, 0)
            target.write_core_register('primask', 1)
            target.write_core_register('xpsr', 0x01000000)
            target.flush()
            flash = target.memory_map.get_boot_memory().flash
            flash.set_flash_algo_debug(True)
            assert target.read16(0x1FFF7A22) == 1024
            result['dbgmcu_idcode'] = hex(target.read32(0xE0042000))
            current = bytes(target.read_memory_block8(0x08000000, len(original)))
            if hashlib.sha256(current).hexdigest() == BACKUP_SHA:
                result['preflash_state'] = 'original_backup_match'
            else:
                assert args.continue_erased_sector0 and current == bytes([255]) * 0x4000 + original[0x4000:], \
                    'Unexpected Flash content; stop before erase'
                result['preflash_state'] = 'sector0_erased_remaining_backup_match'
            # This probe does not reliably catch reset. Prepare while explicitly halted;
            # the standard loader then reuses this initialized erase operation.
            flash.init(flash.Operation.ERASE, reset=False)
            print('START_PROGRAM: Env_hsi16, sector erase only, no automatic unlock', flush=True)
            FileProgrammer(session, chip_erase='sector', smart_flash=False,
                           trust_crc=False, keep_unwritten=False).program(str(IMAGE))
            result['program_returned'] = True
            for start, end in segments:
                actual = bytes(target.read_memory_block8(start, end - start))
                expected = bytes(image.tobinarray(start=start, end=end - 1))
                assert actual == expected, f'Flash mismatch at {start:#x}'
            result['all_hex_bytes_verified'] = True
            tail = bytes(target.read_memory_block8(0x08004000, len(original) - 0x4000))
            assert tail == original[0x4000:], 'Flash outside sector 0 changed'
            result['outside_sector0_unchanged'] = True
            target.reset(Target.ResetType.SYSRESETREQ)
            time.sleep(0.1)
            target.halt()
            target.flush()
            pc = target.read_core_register('pc')
            result['pc_after_reset'] = hex(pc)
            assert 0x08000000 <= pc < 0x08004000, 'Reset did not select the Flash environment program'
            target.resume()
            start_time = time.monotonic()
            for index in range(6):
                if index:
                    time.sleep(1.2)
                sample = {'elapsed_s': round(time.monotonic() - start_time, 3),
                          'state': target.get_state().name,
                          'env_task_progress': target.read32(0x20000000),
                          'env_fault': target.read32(0x20000004),
                          'env_littlefs_version': target.read32(0x20000008),
                          'SystemCoreClock': target.read32(0x2000000C),
                          'uwTick': target.read32(0x20000018),
                          'rtos_tick': target.read32(0x20000028)}
                result['samples'].append(sample)
                print(json.dumps(sample), flush=True)
            samples = result['samples']
            assert all(s['state'] == 'RUNNING' and s['env_fault'] == 0 for s in samples)
            assert all(s['SystemCoreClock'] == 16000000 for s in samples)
            assert all(b['env_task_progress'] > a['env_task_progress']
                       and b['uwTick'] > a['uwTick'] and b['rtos_tick'] > a['rtos_tick']
                       for a, b in zip(samples, samples[1:]))
            result['status'] = 'PASS'
    except Exception as error:
        result['status'] = 'FAIL'
        result['error'] = repr(error)
        raise
    finally:
        result['end'] = datetime.now().astimezone().isoformat()
        OUTPUT.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('RESULT ' + result['status'], flush=True)


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    main()
