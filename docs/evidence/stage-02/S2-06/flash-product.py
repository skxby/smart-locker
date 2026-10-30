"""S2-06 product image programmer for the STM32F407 board.

Same guard rails as the S2-05 bench tool, but the product image spans sectors
0..3 (56 KB), so the untouched window starts at 0x08010000. Lock coil, MOS and
12 V stay disconnected: PB6 is only measured, never driven into a load.

  python docs/evidence/stage-02/S2-06/flash-product.py --identify
  python docs/evidence/stage-02/S2-06/flash-product.py --program --expected-current-sha256 <sha>
  python docs/evidence/stage-02/S2-06/flash-product.py --status
"""
import argparse
import hashlib
import json
import os
import sys
import time
from datetime import datetime
from pathlib import Path

TOOLS = Path('C:/Users/%USER%/Documents/locker-hardware-tools/pyocd-0.46.0')
sys.path.insert(0, str(TOOLS))
os.environ.setdefault('PYOCD_USB_BACKEND', 'pyusb')

from intelhex import IntelHex                                      # noqa: E402
from pyocd.core.helpers import ConnectHelper                       # noqa: E402
from pyocd.core.target import Target                              # noqa: E402

ROOT = Path(__file__).resolve().parents[4]
IMAGE = ROOT / 'build/firmware-s205-gnu/hse8_168/locker_s205.hex'
PACK = Path('E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0')
PROBE = '000000000000000000000000'
FLASH_BASE = 0x08000000
KEEP_FROM = 0x08010000                     # sectors 0..3 belong to the image
SECTOR = 0x4000
# locker_s205 symbol table (build/firmware-s205-gnu/hse8_168/symbols.txt)
TICK = 0x20000088                          # xTickCount
TASKS = 0x2000008C                         # uxCurrentNumberOfTasks
GPIOB_MODER, GPIOB_ODR = 0x40020400, 0x40020414
OPTIONS = {'target_override': 'stm32f407vgtx', 'pack': str(PACK),
           'frequency': 950000, 'connect_mode': 'attach', 'reset_type': 'sysresetreq',
           'auto_unlock': False, 'resume_on_disconnect': False,
           'hide_programming_progress': True, 'pack.debug_sequences.enable': False}


def session():
    s = ConnectHelper.session_with_chosen_probe(blocking=False, unique_id=PROBE, options=OPTIONS)
    if s is None:
        raise RuntimeError('ST-LINK %s not available' % PROBE)
    return s


def prepare_flash_cpu(target):
    target.reset(Target.ResetType.SYSRESETREQ)
    time.sleep(0.1)
    target.halt()
    target.flush()
    assert target.get_state() == Target.State.HALTED
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


def snapshot(target):
    return {'xtick': target.read32(TICK), 'tasks': target.read32(TASKS),
            'gpio_b_moder': hex(target.read32(GPIOB_MODER)),
            'gpio_b_odr': hex(target.read32(GPIOB_ODR))}


def write_json(name, data):
    data = dict(data, when=datetime.now().astimezone().isoformat())
    path = ROOT / 'docs/evidence/stage-02/S2-06' / name
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(data, ensure_ascii=False), flush=True)
    return path


def post_reset_check(target, image_end):
    """Reset into the product image and prove the scheduler and PB6 default."""
    target.reset(Target.ResetType.SYSRESETREQ)
    time.sleep(0.2)
    target.halt()
    target.flush()
    pc = target.read_core_register('pc')
    assert FLASH_BASE <= pc < image_end, 'reset did not land in the product image'
    target.resume()
    time.sleep(0.6)
    target.halt()
    target.flush()
    first = snapshot(target)
    target.resume()
    time.sleep(0.4)
    target.halt()
    target.flush()
    second = snapshot(target)
    target.resume()
    assert second['xtick'] > first['xtick'], 'tick not advancing (scheduler dead)'
    # uxCurrentNumberOfTasks counts the FreeRTOS idle task as well:
    # Control + Communication + Storage + Health + idle = 5.
    assert first['tasks'] == 5, 'expected 5 tasks (4 app + idle), saw %d' % first['tasks']
    assert int(second['gpio_b_odr'], 16) & 0x40 == 0, 'PB6 is not low'
    return pc, first, second


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--identify', action='store_true')
    p.add_argument('--program', action='store_true')
    p.add_argument('--expected-current-sha256')
    p.add_argument('--status', action='store_true')
    p.add_argument('--verify', action='store_true')
    a = p.parse_args()
    if a.program and not a.expected_current_sha256:
        p.error('--program requires --expected-current-sha256 from a prior --identify')
    if not (a.identify or a.program or a.status or a.verify):
        p.error('nothing to do')
    image = IntelHex(str(IMAGE))
    segments = image.segments()
    assert segments and FLASH_BASE <= segments[0][0]
    end = max(b for _, b in segments)
    sectors = [FLASH_BASE + i * SECTOR for i in range((end - FLASH_BASE + SECTOR - 1) // SECTOR)]
    with session() as s:
        t = s.target
        if a.identify:
            cur = bytes(t.read_memory_block8(FLASH_BASE, KEEP_FROM - FLASH_BASE))
            write_json('product-precheck.json', {
                'state': t.get_state().name, 'dbgmcu_idcode': hex(t.read32(0xE0042000)),
                'flash_kb': t.read16(0x1FFF7A22), 'image': str(IMAGE.relative_to(ROOT)),
                'image_segments': [[hex(x), hex(y)] for x, y in segments],
                'sectors_to_erase': [hex(x) for x in sectors],
                'current_head_sha256': hashlib.sha256(cur).hexdigest(),
                'current_keep_sha256': hashlib.sha256(
                    bytes(t.read_memory_block8(KEEP_FROM, 1024 * 1024 - (KEEP_FROM - FLASH_BASE)))).hexdigest()})
        if a.program:
            head = bytes(t.read_memory_block8(FLASH_BASE, KEEP_FROM - FLASH_BASE))
            assert hashlib.sha256(head).hexdigest() == a.expected_current_sha256, \
                'image region changed since --identify'
            keep_before = bytes(t.read_memory_block8(
                KEEP_FROM, 1024 * 1024 - (KEEP_FROM - FLASH_BASE)))
            prepare_flash_cpu(t)
            flash = t.memory_map.get_boot_memory().flash
            flash.set_flash_algo_debug(True)
            flash.init(flash.Operation.ERASE, reset=False)
            assert t.read32(0x40023C10) & 0x80000000 == 0
            print('START_PROGRAM: S2-06 product image, sectors %s' %
                  ', '.join(hex(x) for x in sectors), flush=True)
            for sector in sectors:
                flash.erase_sector(sector)
            flash.uninit()
            prepare_flash_cpu(t)
            flash.init(flash.Operation.PROGRAM, reset=False)
            assert t.read32(0x40023C10) & 0x80000000 == 0
            payload = bytes(image.tobinarray(start=FLASH_BASE, end=image.maxaddr()))
            for offset in range(0, len(payload), 1024):
                flash.program_page(FLASH_BASE + offset,
                                   payload[offset:offset + 1024].ljust(1024, b'\xff'))
            flash.uninit()
            for x, y in segments:
                assert bytes(t.read_memory_block8(x, y - x)) == \
                    bytes(image.tobinarray(start=x, end=y - 1)), 'mismatch %#x' % x
            keep_after = bytes(t.read_memory_block8(
                KEEP_FROM, 1024 * 1024 - (KEEP_FROM - FLASH_BASE)))
            assert keep_after == keep_before, 'flash beyond 0x08010000 changed'
            pc, first, second = post_reset_check(t, end)
            write_json('product-program.json', {
                'status': 'PASS', 'image': str(IMAGE.relative_to(ROOT)),
                'hex_sha256': hashlib.sha256(IMAGE.read_bytes()).hexdigest(),
                'payload_bytes': len(payload), 'sectors_erased': [hex(x) for x in sectors],
                'pc_after_reset': hex(pc), 'first': first, 'second': second})
        if a.verify:
            pc, first, second = post_reset_check(t, end)
            write_json('product-verify.json', {
                'status': 'PASS', 'already_programmed': True,
                'image': str(IMAGE.relative_to(ROOT)),
                'hex_sha256': hashlib.sha256(IMAGE.read_bytes()).hexdigest(),
                'pc_after_reset': hex(pc), 'first': first, 'second': second})
        if a.status:
            t.halt(); t.flush(); data = snapshot(t); t.resume()
            write_json('product-status.json', data)


if __name__ == '__main__':
    main()
