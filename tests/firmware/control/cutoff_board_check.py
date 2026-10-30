"""Restricted empty-load PB6 bench client. Requires physical isolation.

SWD register values are diagnostics, never oscilloscope/power waveform evidence.
No NOR access, format, UART, business command or production qualification.
"""
import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path
from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'journal'))
from board_check import BASE, BACKUP, BACKUP_SHA, expected_image, prepare, sha

NAMES = ['cutoff_command', 'cutoff_pulse_ms', 'cutoff_result', 'cutoff_count',
         'cutoff_progress', 'cutoff_phase', 'cutoff_fault']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['program', 'inspect', 'pulse', 'reset'])
    p.add_argument('--image', type=Path, required=True)
    p.add_argument('--image-sha256', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--prior-evidence', type=Path)
    p.add_argument('--prior-image', type=Path)
    p.add_argument('--isolated', action='store_true', help='Operator confirmed coil/MOS12V and PB6-MOS signal isolated')
    p.add_argument('--mode', type=int, choices=[1, 2, 3, 4], default=1)
    p.add_argument('--pulse-ms', type=int, default=1000)
    a = p.parse_args()
    if not a.isolated or a.output.exists() or sha(a.image.read_bytes()) != a.image_sha256:
        p.error('Require confirmed isolation, new evidence path and exact HEX hash')
    if not 1 <= a.pulse_ms <= 1000 or (a.mode in (2, 3) and a.pulse_ms > 100):
        p.error('Pulse must be 1..1000ms; interrupt/scheduler stall modes at most 100ms')
    with a.image.with_suffix('.elf').open('rb') as f:
        table = ELFFile(f).get_section_by_name('.symtab')
        found = {s.name: s['st_value'] for s in table.iter_symbols()}
    addresses = {n: found[n] for n in NAMES}
    if any(not 0x20000000 <= v <= 0x2001fffc for v in addresses.values()):
        raise ValueError('Unexpected bench mailbox addresses')
    result = {'started': datetime.now().astimezone().isoformat(), 'action': a.action,
              'image': str(a.image.resolve()), 'image_sha256': a.image_sha256,
              'isolated': True, 'electrical_waveform_verified': False, 'production_profile_verified': False,
              'status': 'IN_PROGRESS'}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('x', encoding='utf-8') as evidence:
        try:
            options = {'target_override': 'stm32f407vgtx',
                       'pack': 'E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0',
                       'frequency': 950000, 'connect_mode': 'attach', 'auto_unlock': False,
                       'resume_on_disconnect': False, 'pack.debug_sequences.enable': False,
                       'hide_programming_progress': True, 'reset_type': 'sysresetreq'}
            session = ConnectHelper.session_with_chosen_probe(blocking=False,
                unique_id='000000000000000000000000', options=options)
            if session is None:
                raise RuntimeError('Verified ST-Link unavailable')
            with session:
                t = session.target
                uid = bytes(t.read_memory_block8(0x1fff7a10, 12)).hex()
                if uid != '000000000000000000000000' or t.read16(0x1fff7a22) != 1024:
                    raise RuntimeError('Unexpected MCU')
                result['uid'] = uid
                original = BACKUP.read_bytes()
                if len(original) != 1048576 or sha(original) != BACKUP_SHA:
                    raise RuntimeError('Original MCU backup missing/changed')
                desired, extent = expected_image(original, a.image)
                if a.action == 'program':
                    if not a.prior_evidence or not a.prior_image:
                        raise ValueError('Programming requires prior evidence and preserved HEX')
                    prior = json.loads(a.prior_evidence.read_text())
                    if prior['status'] != 'PASS' or sha(a.prior_image.read_bytes()) != prior['image_sha256']:
                        raise ValueError('Prior image/evidence mismatch')
                    expected, old_extent = expected_image(original, a.prior_image)
                    prepare(t)
                    current = bytes(t.read_memory_block8(BASE, len(original)))
                    if current != expected:
                        raise RuntimeError('Full current MCU image differs from prior guarded state')
                    result['preflash_sha256'] = sha(current)
                    window = ((max(extent, old_extent) + 16383) // 16384) * 16384
                    flash = t.memory_map.get_boot_memory().flash
                    flash.set_flash_algo_debug(True)
                    flash.init(flash.Operation.ERASE, reset=False)
                    try:
                        for offset in range(0, window, 16384):
                            flash.erase_sector(BASE + offset)
                    finally:
                        flash.uninit()
                    prepare(t)
                    flash.init(flash.Operation.PROGRAM, reset=False)
                    try:
                        for offset in range(0, window, 1024):
                            flash.program_page(BASE + offset, desired[offset:offset + 1024])
                    finally:
                        flash.uninit()
                    if bytes(t.read_memory_block8(BASE, len(original))) != desired:
                        raise RuntimeError('Full MCU readback differs')
                    result.update(readback_verified=True, original_tail_preserved=True,
                                  internal_flash_window_bytes=window, postflash_sha256=sha(desired))
                    t.set_vector_catch(Target.VectorCatch.NONE)
                    t.reset(Target.ResetType.SYSRESETREQ)
                    if t.get_state() == Target.State.HALTED:
                        t.resume()
                    time.sleep(.5)
                else:
                    if bytes(t.read_memory_block8(BASE, len(original))) != desired:
                        raise RuntimeError('Current full image is not the guarded bench firmware')
                    if a.action == 'reset':
                        t.set_vector_catch(Target.VectorCatch.NONE)
                        t.reset(Target.ResetType.SYSRESETREQ)
                        if t.get_state() == Target.State.HALTED:
                            t.resume()
                        time.sleep(.5)
                if t.get_state() != Target.State.RUNNING:
                    raise RuntimeError('MCU not running')

                def sample():
                    v = {n: t.read32(addresses[n]) for n in NAMES}
                    if v['cutoff_fault']:
                        raise RuntimeError('Bench firmware fault')
                    return v

                result['before'] = sample()
                if a.action == 'pulse':
                    before = result['before']
                    if before['cutoff_command'] or before['cutoff_phase']:
                        raise RuntimeError('Bench mailbox busy')
                    t.write32(addresses['cutoff_pulse_ms'], a.pulse_ms)
                    t.write32(addresses['cutoff_command'], a.mode)
                    t.flush()
                    deadline = time.monotonic() + 15
                    while True:
                        v = sample()
                        if v['cutoff_count'] == before['cutoff_count'] + 1 and not v['cutoff_phase'] and not v['cutoff_command']:
                            break
                        if v['cutoff_result'] != 0:
                            raise RuntimeError('Pulse rejected: ' + str(v))
                        if time.monotonic() > deadline:
                            raise TimeoutError('Bench command did not complete')
                        time.sleep(.025)
                    time.sleep(a.pulse_ms / 1000 + .1)
                    result.update(mode=a.mode, planned_pulse_ms=a.pulse_ms)
                result['after'] = sample()
                result['timer4'] = {n: t.read32(0x40000800 + offset) for n, offset in
                    [('CR1', 0), ('DIER', 0x0c), ('CCMR1', 0x18), ('CCER', 0x20),
                     ('CNT', 0x24), ('PSC', 0x28), ('ARR', 0x2c), ('CCR1', 0x34)]}
                result['pb6_idr'] = (t.read32(0x40020410) >> 6) & 1
                if result['timer4']['CR1'] & 1 or result['pb6_idr']:
                    raise RuntimeError('Output has not returned low/stopped')
                if a.action == 'pulse' and a.mode != 4:
                    regs = result['timer4']
                    if not regs['CR1'] & 8 or regs['ARR'] != a.pulse_ms * 10 or regs['CCR1'] != 1 or regs['DIER']:
                        raise RuntimeError('Unexpected hardware single-pulse configuration')
                result['status'] = 'PASS'
        except BaseException as exc:
            result['status'] = 'FAIL'
            result['error'] = repr(exc)
            raise
        finally:
            result['finished'] = datetime.now().astimezone().isoformat()
            json.dump(result, evidence, ensure_ascii=False, indent=2)
            evidence.write('\n')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
