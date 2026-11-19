"""S2-07 journal epoch maintenance on the real board (archive first, then new epoch).

The floor journal lives on the external W25Q128 NOR (16 MiB, JEDEC 0xef4018),
not in MCU flash. journal.c latches continuity_ok=false for the whole epoch once
a boot is found with boot_open set, and only mcu_journal_provision() clears it.
So a new epoch erases that NOR filesystem: this tool archives the existing
facts first and refuses to provision in the same run as the archive.

Bench image = the S2-04 validation build, whose s204_* symbols drive the real
journal API (1 raw NOR read, 2 provision, 3 mount, 9 shutdown, 10 read page,
11 recover). Nothing here drives the lock: PB6 is only read.

  python journal-maintenance.py --identify
  python journal-maintenance.py --program-bench --expected-current-sha256 <sha>
  python journal-maintenance.py --backup-nor build/hardware-S2-07/journal/nor-before.json.bin
  python journal-maintenance.py --cmd 3      # mount (adds a BOOT fact)
  python journal-maintenance.py --cmd 11     # recover
  python journal-maintenance.py --cmd 10 --out page-01
  python journal-maintenance.py --cmd 9      # clean shutdown
  python journal-maintenance.py --cmd 2 --epoch <uuid>
  python journal-maintenance.py --restore-product --expected-current-sha256 <sha>
"""
import argparse
import hashlib
import json
import os
import re
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
BENCH_IMAGE = ROOT / 'build/firmware-s204-gnu/hse8_168/locker_s204.hex'
PRODUCT_IMAGE = ROOT / 'build/firmware-s205-gnu/hse8_168/locker_s205.hex'
EVIDENCE = ROOT / 'docs/evidence/stage-02/S2-07'
PACK = Path('E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0')
PROBE = '000000000000000000000000'

FLASH_BASE = 0x08000000
HEAD_BYTES = 0x10000
SECTOR = 0x4000
NOR_BYTES = 16777216
NOR_JEDEC = 0xEF4018

# s204_* symbols from build/firmware-s204-gnu/hse8_168/locker_s204.map
DIAG, COMMAND, DATA = 0x200073FC, 0x200053F8, 0x200053FC
ADDRESS, LENGTH = 0x200053F4, 0x200053F0
DATA_BYTES = 8192
DIAG_WORDS = 32


def map_symbols(path):
    """Resolve .bss locals from the linker map instead of hardcoding addresses.

    Adding one global to main.c shifted every later .bss symbol; the previous
    hardcoded TICK/TASKS then pointed at unrelated bytes and reported
    "scheduler dead" for a perfectly healthy board.
    """
    symbols = {}
    try:
        lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
    except OSError:
        return symbols
    for index, line in enumerate(lines):
        match = re.match(r'\s*\.bss\.(\w+)\s*$', line)
        if match and index + 1 < len(lines):
            found = re.search(r'0x([0-9a-fA-F]{8})', lines[index + 1])
            if found:
                symbols[match.group(1)] = int(found.group(1), 16)
    return symbols


SYMBOLS = map_symbols(PRODUCT_IMAGE.with_suffix('.map'))
TICK = SYMBOLS.get('xTickCount', 0x2000008C)
TASKS = SYMBOLS.get('uxCurrentNumberOfTasks', 0x20000090)
GPIOB_MODER, GPIOB_ODR = 0x40020400, 0x40020414

COMMAND_NAMES = {1: 'NOR_READ', 2: 'PROVISION', 3: 'MOUNT', 4: 'APPEND_HEALTH',
                 5: 'DIAG_INTENT', 6: 'FINALIZE', 7: 'RELEASE', 8: 'ACK',
                 9: 'SHUTDOWN', 10: 'READ_PAGE', 11: 'RECOVER'}
JOURNAL_STATES = {0: 'UNINITIALIZED', 1: 'HEALTHY', 2: 'DEGRADED', 3: 'FAULT', 4: 'FULL'}

OPTIONS = {'target_override': 'stm32f407vgtx', 'pack': str(PACK),
           'frequency': 950000, 'connect_mode': 'attach', 'reset_type': 'sysresetreq',
           'auto_unlock': False, 'resume_on_disconnect': False,
           'hide_programming_progress': True, 'pack.debug_sequences.enable': False}


def session():
    s = ConnectHelper.session_with_chosen_probe(blocking=False, unique_id=PROBE, options=OPTIONS)
    if s is None:
        raise RuntimeError('ST-LINK %s not available' % PROBE)
    return s


def write_json(name, data):
    data = dict(data, when=datetime.now().astimezone().isoformat())
    path = EVIDENCE / (name + '.json')
    if path.exists():
        raise SystemExit('%s exists; pick a new --out name' % path)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(data, ensure_ascii=False), flush=True)
    return path


def prepare_flash_cpu(target):
    """Same clock/Flash-wait preconditions the Keil algorithm needs."""
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


def sectors_for(image):
    end = max(b for _, b in image.segments())
    return [FLASH_BASE + i * SECTOR
            for i in range((end - FLASH_BASE + SECTOR - 1) // SECTOR)], end


def program_image(target, path, expected_current_sha256, label):
    image = IntelHex(str(path))
    segments = image.segments()
    sectors, end = sectors_for(image)
    keep_from = FLASH_BASE + len(sectors) * SECTOR
    # Verify a fixed window so one --identify value fits either image.
    head = bytes(target.read_memory_block8(FLASH_BASE, HEAD_BYTES))
    assert hashlib.sha256(head).hexdigest() == expected_current_sha256, \
        'image region changed since the survey; re-identify'
    keep_before = bytes(target.read_memory_block8(keep_from, 1024 * 1024 - (keep_from - FLASH_BASE)))
    prepare_flash_cpu(target)
    flash = target.memory_map.get_boot_memory().flash
    flash.set_flash_algo_debug(True)
    flash.init(flash.Operation.ERASE, reset=False)
    assert target.read32(0x40023C10) & 0x80000000 == 0
    print('START_PROGRAM: %s sectors %s' % (label, ', '.join(hex(x) for x in sectors)), flush=True)
    for sector in sectors:
        flash.erase_sector(sector)
    flash.uninit()
    prepare_flash_cpu(target)
    flash.init(flash.Operation.PROGRAM, reset=False)
    assert target.read32(0x40023C10) & 0x80000000 == 0
    payload = bytes(image.tobinarray(start=FLASH_BASE, end=image.maxaddr()))
    for offset in range(0, len(payload), 1024):
        flash.program_page(FLASH_BASE + offset, payload[offset:offset + 1024].ljust(1024, b'\xff'))
    flash.uninit()
    for x, y in segments:
        assert bytes(target.read_memory_block8(x, y - x)) == \
            bytes(image.tobinarray(start=x, end=y - 1)), 'Flash mismatch %#x' % x
    keep_after = bytes(target.read_memory_block8(keep_from, 1024 * 1024 - (keep_from - FLASH_BASE)))
    assert keep_after == keep_before, 'flash outside the image changed'
    target.reset(Target.ResetType.SYSRESETREQ)
    time.sleep(0.2)
    target.halt()
    target.flush()
    pc = target.read_core_register('pc')
    assert FLASH_BASE <= pc < end, 'reset did not land in the image'
    target.resume()
    return {'status': 'PASS', 'image': str(path.relative_to(ROOT)),
            'hex_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'payload_bytes': len(payload), 'sectors_erased': [hex(x) for x in sectors],
            'pc_after_reset': hex(pc), 'replaced_head_sha256': expected_current_sha256}


def diag(target):
    return [int(x) for x in target.read_memory_block32(DIAG, DIAG_WORDS * 4)]


def decode(words):
    def pair(index):
        return words[index] | (words[index + 1] << 32)
    return {'phase': words[0], 'last_result': words[1], 'flash_init': words[2],
            'jedec_id': hex(words[3]), 'has_journal': words[4],
            'boot_id': str(pair(5)), 'action_watermark': str(pair(7)),
            'has_active': words[9], 'command_state': words[10],
            'event_count': words[11],
            'first_available': [str(pair(12)), str(pair(14))],
            'last_committed': [str(pair(16)), str(pair(18))],
            'ack_persisted': [str(pair(20)), str(pair(22))],
            'journal_state': JOURNAL_STATES.get(words[29], words[29]),
            'continuity_ok': words[30], 'commands_served': words[31], 'raw': words[:DIAG_WORDS]}


def wait_bench_ready(target, timeout=20.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        words = diag(target)
        if words[0] >= 2 and words[2] == 0:
            return words
        time.sleep(0.2)
    raise RuntimeError('bench storage task did not reach ready (diag[0]=%d)' % diag(target)[0])


def issue(target, number, timeout=180.0):
    """One-shot handshake: the storage task clears s204_command when finished."""
    base = diag(target)[0]
    target.write32(COMMAND, number)
    target.flush()
    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(0.1)
        words = diag(target)
        if words[0] >= base + 2 and target.read32(COMMAND) == 0:
            return decode(words)
    raise RuntimeError('command %d timed out; do not replay blindly' % number)


def read_nor(target, total, sink):
    digest = hashlib.sha256()
    done = 0
    started = time.time()
    while done < total:
        chunk = min(DATA_BYTES, total - done)
        target.write32(ADDRESS, done)
        target.write32(LENGTH, chunk)
        target.flush()
        state = issue(target, 1, timeout=30.0)
        assert state['last_result'] == 0, 'NOR read failed at %#x (result %d)' % (done, state['last_result'])
        block = bytes(target.read_memory_block8(DATA, chunk))
        sink.write(block)
        digest.update(block)
        done += chunk
        if done % (512 * 1024) == 0:
            print('  read %d/%d bytes (%.0fs)' % (done, total, time.time() - started), flush=True)
    return digest.hexdigest()


def post_reset_check(target, image_end):
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
    first = {'xtick': target.read32(TICK), 'tasks': target.read32(TASKS),
             'gpio_b_moder': hex(target.read32(GPIOB_MODER)), 'gpio_b_odr': hex(target.read32(GPIOB_ODR))}
    target.resume()
    time.sleep(0.4)
    target.halt()
    target.flush()
    second = {'xtick': target.read32(TICK), 'tasks': target.read32(TASKS),
              'gpio_b_moder': hex(target.read32(GPIOB_MODER)), 'gpio_b_odr': hex(target.read32(GPIOB_ODR))}
    target.resume()
    assert second['xtick'] > first['xtick'], 'tick not advancing (scheduler dead)'
    assert first['tasks'] == 5, 'expected 5 tasks, saw %d' % first['tasks']
    assert int(second['gpio_b_odr'], 16) & 0x40 == 0, 'PB6 is not low'
    return pc, first, second


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--identify', action='store_true')
    p.add_argument('--program-bench', action='store_true')
    p.add_argument('--restore-product', action='store_true')
    p.add_argument('--expected-current-sha256')
    p.add_argument('--backup-nor', type=Path, help='Destination .bin for the whole 16 MiB NOR')
    p.add_argument('--cmd', type=int, help='Bench command number (see COMMAND_NAMES)')
    p.add_argument('--epoch', help='New epoch UUID; written to s204_data before --cmd 2')
    p.add_argument('--out', default='board-step', help='Evidence file stem for --cmd')
    p.add_argument('--page-bytes', type=int, default=0, help='After --cmd 10, also dump s204_data')
    a = p.parse_args()
    if (a.program_bench or a.restore_product) and not a.expected_current_sha256:
        p.error('--program-bench/--restore-product require --expected-current-sha256')
    if not (a.identify or a.program_bench or a.restore_product or a.backup_nor or a.cmd):
        p.error('nothing to do')

    with session() as s:
        t = s.target
        if a.identify:
            head = bytes(t.read_memory_block8(FLASH_BASE, HEAD_BYTES))
            write_json('board-survey' if a.out == 'board-step' else a.out + '-survey', {
                'state': t.get_state().name, 'dbgmcu_idcode': hex(t.read32(0xE0042000)),
                'flash_kb': t.read16(0x1FFF7A22),
                'head_0x10000_sha256': hashlib.sha256(head).hexdigest(),
                'whole_internal_sha256': hashlib.sha256(
                    bytes(t.read_memory_block8(FLASH_BASE, 1024 * 1024))).hexdigest()})
        if a.program_bench:
            record = program_image(t, BENCH_IMAGE, a.expected_current_sha256, 'S2-07 journal bench (S2-04 build)')
            words = wait_bench_ready(t)
            record['bench_diag'] = decode(words)
            assert words[3] == NOR_JEDEC, 'unexpected NOR JEDEC %#x' % words[3]
            write_json('bench-program', record)
        if a.backup_nor:
            a.backup_nor.parent.mkdir(parents=True, exist_ok=True)
            with open(a.backup_nor, 'xb') as sink:
                sha = read_nor(t, NOR_BYTES, sink)
            write_json('nor-backup', {'status': 'PASS', 'file': str(a.backup_nor),
                                      'bytes': NOR_BYTES, 'sha256': sha,
                                      'diag': decode(diag(t))})
        if a.cmd:
            if a.epoch:
                raw = bytes.fromhex(a.epoch.replace('-', ''))
                assert len(raw) == 16 and any(raw), 'epoch must be a nonzero 16-byte UUID'
                t.write_memory_block8(DATA, raw)
                t.flush()
            state = issue(t, a.cmd, timeout=300.0 if a.cmd == 2 else 120.0)
            record = {'command': a.cmd, 'name': COMMAND_NAMES.get(a.cmd, '?'), 'diag': state}
            if a.cmd == 10 and a.page_bytes:
                record['page_hex'] = bytes(t.read_memory_block8(DATA, a.page_bytes)).hex()
            print(json.dumps(record, ensure_ascii=False), flush=True)
            write_json(a.out, record)
        if a.restore_product:
            record = program_image(t, PRODUCT_IMAGE, a.expected_current_sha256, 'S2-06 product image')
            end = max(b for _, b in IntelHex(str(PRODUCT_IMAGE)).segments())
            pc, first, second = post_reset_check(t, end)
            record.update({'pc_after_reset': hex(pc), 'first': first, 'second': second})
            write_json('product-restore', record)


if __name__ == '__main__':
    main()
