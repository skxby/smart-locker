"""S2-05 isolated PB6 pulse bench. Lock coil, MOS and 12 V stay disconnected.

Every memory write is an explicit, one-shot SWD mailbox command serviced by the
Control task; the pin is never left driven by this tool. Programming requires
--program plus the SHA-256 of the image currently in sector 0.

  python docs/evidence/stage-02/S2-05/flash-and-trigger.py --identify
  python docs/evidence/stage-02/S2-05/flash-and-trigger.py --program --expected-current-sha256 <sha>
  python docs/evidence/stage-02/S2-05/flash-and-trigger.py --status
  python docs/evidence/stage-02/S2-05/flash-and-trigger.py --trigger 1 --pulse-ms 1000
"""
import argparse
import hashlib
import json
import os
import sys
import time
from datetime import datetime
from pathlib import Path

# pyOCD 0.46.0 is a separately installed tool outside the repo; the Windows
# hidapi wheel is unusable here, so the pyusb backend is selected explicitly.
TOOLS = Path('C:/Users/%USER%/Documents/locker-hardware-tools/pyocd-0.46.0')
sys.path.insert(0, str(TOOLS))
os.environ.setdefault('PYOCD_USB_BACKEND', 'pyusb')

from intelhex import IntelHex                                      # noqa: E402
from pyocd.core.helpers import ConnectHelper                       # noqa: E402
from pyocd.core.target import Target                              # noqa: E402

ROOT = Path(__file__).resolve().parents[4]
IMAGE = ROOT / 'build/firmware-s205-cutoff-gnu/hse8_168/locker_s205-cutoff.hex'
PACK = Path('E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0')
PROBE = '000000000000000000000000'
SECTOR0 = 0x08000000
SUFFIX = ''            # set from --suffix; keeps re-runs from reusing evidence names
FORCE = False          # set from --force; closed-card evidence stays immutable by default

# Free-standing diagnostic symbols of control/cutoff_validation.c.
SYM = {'pulse_ms': 0x20000000, 'fault': 0x20000018, 'phase': 0x2000001C,
       'progress': 0x20000020, 'count': 0x20000024, 'result': 0x20000028,
       'command': 0x2000002C}
STATUS = {0: 'OK', 1: 'ACCEPTED', 2: 'QUEUED', 3: 'INVALID', 4: 'BUSY',
          5: 'NOT_READY', 6: 'CONFLICT', 7: 'UNKNOWN', 8: 'IO_ERROR'}
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
    """Known clock/HSE/Flash-wait state before the unmodified Keil FLM runs."""
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
    return {'command': target.read32(SYM['command']), 'pulse_ms': target.read32(SYM['pulse_ms']),
            'progress': target.read32(SYM['progress']), 'count': target.read32(SYM['count']),
            'result': STATUS.get(target.read32(SYM['result']), 'UNKNOWN_CODE'),
            'phase': target.read32(SYM['phase']), 'fault': target.read32(SYM['fault']),
            'gpio_b_moder': hex(target.read32(0x40020400)), 'gpio_b_odr': hex(target.read32(0x40020414))}


def write_json(name, data, force=False):
    data = dict(data, when=datetime.now().astimezone().isoformat())
    stem, _, ext = name.partition('.')
    path = ROOT / 'docs/evidence/stage-02/S2-05' / ((stem + SUFFIX + '.' + ext) if SUFFIX else name)
    # S2-05 is a closed card: never let a later survey silently rewrite its record.
    if path.exists() and not (force or FORCE):
        raise SystemExit('%s exists; new evidence needs a new name (--out/--suffix) '
                         'or an explicit --force' % path)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(data, ensure_ascii=False), flush=True)
    return path


def identify(target):
    current = bytes(target.read_memory_block8(SECTOR0, 0x4000))
    info = {'probe': PROBE, 'dbgmcu_idcode': hex(target.read32(0xE0042000)),
            'flash_kb': target.read16(0x1FFF7A22),
            'state': target.get_state().name,
            'sector0_sha256': hashlib.sha256(current).hexdigest(),
            'sector0_ff_padded': current.rstrip(b'\xff') == b''}
    if target.get_state() == Target.State.HALTED:
        info['pc'] = hex(target.read_core_register('pc'))
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--identify', action='store_true', help='Read-only probe and Flash survey')
    parser.add_argument('--program', action='store_true', help='Authorize sector-0 programming')
    parser.add_argument('--verify-only', action='store_true',
                        help='Image already written by an interrupted run: verify and run it')
    parser.add_argument('--expected-current-sha256', help='SHA-256 of the sector-0 image being replaced')
    parser.add_argument('--status', action='store_true', help='Read the bench mailbox without writing')
    parser.add_argument('--trigger', type=int, choices=(1, 2, 3, 4, 5),
                        help='1 plain, 2 IRQs masked, 3 scheduler suspended, 4 software off, 5 system reset')
    parser.add_argument('--pulse-ms', type=int, default=1000, help='Requested pulse width, 1..1000')
    parser.add_argument('--settle', type=float, default=0.0, help='Delay before the board is read back')
    parser.add_argument('--suffix', default='',
                        help='Append to every evidence file name, e.g. --suffix=-rerun01')
    parser.add_argument('--force', action='store_true',
                        help='Overwrite an existing S2-05 evidence file (never for a closed card)')
    args = parser.parse_args()
    if args.program and not args.expected_current_sha256:
        parser.error('--program requires --expected-current-sha256 from a prior --identify')
    if not (args.identify or args.program or args.verify_only or args.status or args.trigger):
        parser.error('nothing to do')
    global SUFFIX, FORCE
    SUFFIX = args.suffix
    FORCE = args.force

    s = session()
    with s:
        target = s.target
        if args.identify:
            write_json('board-precheck.json', identify(target))
        if args.program or args.verify_only:
            summary = json.loads((IMAGE.parent / 'summary.json').read_text(encoding='utf-8'))
            image = IntelHex(str(IMAGE))
            segments = image.segments()
            assert segments and all(SECTOR0 <= a < b <= SECTOR0 + 0x4000 for a, b in segments)
            payload = bytes(image.tobinarray(start=SECTOR0, end=image.maxaddr()))
            current = bytes(target.read_memory_block8(SECTOR0, 0x4000))
            already = current[:len(payload)] == payload and current[len(payload):] == b'\xff' * (0x4000 - len(payload))
            if args.program:
                assert hashlib.sha256(current).hexdigest() == args.expected_current_sha256, \
                    'sector 0 changed since --identify; re-run --identify'
                tail_before = bytes(target.read_memory_block8(SECTOR0 + 0x4000, 1024 * 1024 - 0x4000))
                prepare_flash_cpu(target)
                flash = target.memory_map.get_boot_memory().flash
                flash.set_flash_algo_debug(True)
                flash.init(flash.Operation.ERASE, reset=False)
                assert target.read32(0x40023C10) & 0x80000000 == 0
                print('START_PROGRAM: S2-05 isolated pulse bench, sector 0 only', flush=True)
                flash.erase_sector(SECTOR0)
                flash.uninit()
                prepare_flash_cpu(target)
                flash.init(flash.Operation.PROGRAM, reset=False)
                assert target.read32(0x40023C10) & 0x80000000 == 0
                for offset in range(0, len(payload), 1024):
                    flash.program_page(SECTOR0 + offset, payload[offset:offset + 1024].ljust(1024, b'\xff'))
                flash.uninit()
                for start, end in segments:
                    assert bytes(target.read_memory_block8(start, end - start)) == \
                        bytes(image.tobinarray(start=start, end=end - 1)), 'Flash mismatch %#x' % start
                tail_after = bytes(target.read_memory_block8(SECTOR0 + 0x4000, 1024 * 1024 - 0x4000))
                assert tail_after == tail_before, 'Flash outside sector 0 changed'
            else:
                # An interrupted run already left the exact image in sector 0.
                assert already, 'sector 0 does not hold the bench image; use --program'
            target.reset(Target.ResetType.SYSRESETREQ)
            time.sleep(0.2)
            target.halt()
            target.flush()
            pc = target.read_core_register('pc')
            assert SECTOR0 <= pc < SECTOR0 + 0x4000, 'Reset did not land in the bench image'
            target.resume()
            time.sleep(0.5)
            target.halt()
            target.flush()
            first = snapshot(target)
            target.resume()
            time.sleep(0.3)
            target.halt()
            target.flush()
            second = snapshot(target)
            target.resume()
            assert first['fault'] == 0 and second['progress'] > first['progress'], \
                'Control task is not running (fault=%d)' % first['fault']
            assert first['result'] == 'OK', 'board_lock_apply rejected: %s' % first['result']
            assert second['gpio_b_odr'] == '0x0' or int(second['gpio_b_odr'], 16) & 0x40 == 0, \
                'PB6 is not low after apply'
            write_json('program.json', {'status': 'PASS', 'image': str(IMAGE.relative_to(ROOT)),
                                        'image_sha256': summary['image_sha256'],
                                        'hex_sha256': hashlib.sha256(IMAGE.read_bytes()).hexdigest(),
                                        'replaced_sector0_sha256': args.expected_current_sha256,
                                        'sector0_sha256': hashlib.sha256(
                                            bytes(target.read_memory_block8(SECTOR0, 0x4000))).hexdigest(),
                                        'already_programmed': already and not args.program,
                                        'segments': [[hex(a), hex(b)] for a, b in segments],
                                        'pc_after_reset': hex(pc), 'memory': summary['memory'],
                                        'first': first, 'second': second})
        if args.status:
            target.halt()
            target.flush()
            data = snapshot(target)
            target.resume()
            write_json('board-status.json', data)
        if args.trigger:
            assert 1 <= args.pulse_ms <= 1000
            target.halt()
            target.flush()
            before = snapshot(target)
            target.write32(SYM['pulse_ms'], args.pulse_ms)
            target.write32(SYM['command'], args.trigger)
            target.flush()
            target.resume()
            time.sleep(max(args.settle, 0.02) + args.pulse_ms / 1000.0 + 0.9)
            try:
                target.halt()
                target.flush()
                after = snapshot(target)
                target.resume()
            except Exception as exc:                       # mode 5 resets the core
                after = {'readback_error': '%s: %s' % (type(exc).__name__, exc)}
            write_json('trigger-%d-%dms.json' % (args.trigger, args.pulse_ms),
                       {'status': 'DONE', 'mode': args.trigger, 'pulse_ms': args.pulse_ms,
                        'before': before, 'after': after})


if __name__ == '__main__':
    main()
