"""Restricted S2-04 SWD validation client. No public command or lock control.

Use the retained pyOCD 0.46.0 tool environment, not product dependencies.
Every run creates new JSON evidence. Programming guards the full current MCU
image against the accepted S203 image plus the original backup tail. SPI NOR
provisioning requires the separately verified full backup from this tool.
"""
import argparse
import hashlib
import json
import re
import time
import uuid
from datetime import datetime
from pathlib import Path

from intelhex import IntelHex
from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target

ROOT = Path(__file__).resolve().parents[3]
BASE = 0x08000000
CAPACITY = 16 * 1024 * 1024
BACKUP = Path.home() / 'Documents/locker-hardware-backups/2026-10-28/stm32f407-original-121141.bin'
BACKUP_SHA = 'e893d0f0235dac60fa12d9b884486c65adf50a9d589e5ecaa45120f0b3df9ffa'
OLD_EVIDENCE = ROOT / 'docs/evidence/stage-02/S2-03/board-uart-startup-02.json'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def symbols(image):
    elf = image.with_suffix('.elf')
    if elf.exists():
        from elftools.elf.elffile import ELFFile
        with elf.open('rb') as source:
            table = ELFFile(source).get_section_by_name('.symtab')
            found = {symbol.name: symbol['st_value'] for symbol in table.iter_symbols()}
        names = ['s204_diag', 's204_command', 's204_address', 's204_length', 's204_data', 's204_fault', 's204_state', 's204_page']
        result = {name: found[name] for name in names}
        if any(not 0x20000000 <= address < 0x20020000 for address in result.values()) or result['s204_data'] + 8192 > 0x20020000:
            raise ValueError('ELF mailbox outside main SRAM')
        return result
    body = image.with_suffix('.map').read_text(encoding='utf-8', errors='replace')
    result = {}
    for name in ['s204_diag', 's204_command', 's204_address', 's204_length', 's204_data', 's204_fault', 's204_state', 's204_page']:
        match = re.search(r'^\s*' + name + r'\s+(0x[0-9a-fA-F]+)\s+Data\s+', body, re.M)
        if not match:
            raise ValueError('Missing symbol ' + name)
        result[name] = int(match.group(1), 16)
        if not 0x20000000 <= result[name] < 0x20020000:
            raise ValueError('Symbol outside main SRAM')
    if result['s204_data'] + 8192 > 0x20020000:
        raise ValueError('Mailbox exceeds main SRAM')
    return result


def expected_image(original, path):
    image = IntelHex(str(path))
    if any(a < BASE or b > BASE + 65536 for a, b in image.segments()):
        raise ValueError('Image outside guarded first four sectors')
    extent = image.maxaddr() - BASE + 1
    result = bytearray(b'\xff' * extent + original[extent:])
    for address in image.addresses():
        result[address - BASE] = image[address]
    return bytes(result), extent


def prepare(target):
    # Catch reset before the existing FreeRTOS image can select PSP. Flash
    # algorithms require a clean privileged thread context and their own MSP.
    target.reset_and_halt(Target.ResetType.SYSRESETREQ)
    target.write_core_register('control', 0)
    target.write_core_register('basepri', 0)
    target.write_core_register('faultmask', 0)
    target.write_core_register('primask', 1)
    if target.read_core_register('ipsr') != 0:
        raise RuntimeError('Flash loader did not enter a reset thread context')
    target.write32(0x40023C00, (target.read32(0x40023C00) & ~7) | 5)
    target.write32(0x40023800, target.read32(0x40023800) | 1)
    target.flush()
    if not target.read32(0x40023800) & 2:
        raise RuntimeError('HSI unavailable')
    target.write32(0x40023808, target.read32(0x40023808) & ~3)
    target.flush()
    time.sleep(.02)
    if target.read32(0x40023808) & 12:
        raise RuntimeError('HSI switch failed')
    target.write32(0xE000E010, 0)
    target.write_core_register('primask', 1)
    target.write_core_register('xpsr', 0x01000000)
    target.flush()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['program', 'backup', 'provision', 'commands', 'inspect', 'reset'])
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--image-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--backup', type=Path)
    parser.add_argument('--backup-evidence', type=Path)
    parser.add_argument('--current-evidence', type=Path, default=OLD_EVIDENCE)
    parser.add_argument('--current-image', type=Path,
                        help='Preserved previous HEX when a build path has been reused')
    parser.add_argument('--commands', default='3,11,10')
    parser.add_argument('--expected-result', type=int, default=0,
                        help='Expected status for each explicit diagnostic command')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Evidence exists; choose a new output')
    if sha(args.image.read_bytes()) != args.image_sha256:
        parser.error('Image hash mismatch')
    addresses = symbols(args.image)
    result = {'started': datetime.now().astimezone().isoformat(), 'action': args.action,
              'image': str(args.image.resolve()), 'image_sha256': args.image_sha256,
              'status': 'IN_PROGRESS', 'commands': [], 'lock_connected': False,
              'lock_isolation_basis': 'user confirmation in this chat'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8') as evidence:
        try:
            options = {'target_override': 'stm32f407vgtx',
                       'pack': 'E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0',
                       'frequency': 950000, 'connect_mode': 'attach', 'auto_unlock': False,
                       'resume_on_disconnect': False, 'pack.debug_sequences.enable': False,
                       'hide_programming_progress': True, 'reset_type': 'sysresetreq'}
            session = ConnectHelper.session_with_chosen_probe(blocking=False,
                unique_id='000000000000000000000000', options=options)
            if session is None:
                raise RuntimeError('Verified ST-LINK unavailable')
            with session:
                target = session.target
                if target.read16(0x1fff7a22) != 1024:
                    raise RuntimeError('Unexpected MCU Flash capacity')
                result['uid'] = bytes(target.read_memory_block8(0x1fff7a10, 12)).hex()
                if args.action == 'program':
                    original = BACKUP.read_bytes()
                    if len(original) != 1048576 or sha(original) != BACKUP_SHA:
                        raise RuntimeError('Original backup missing/changed')
                    prior = json.loads(args.current_evidence.read_text())
                    old_path = args.current_image or Path(prior['image'])
                    if prior['status'] != 'PASS' or sha(old_path.read_bytes()) != prior['image_sha256']:
                        raise RuntimeError('Previous tested firmware changed')
                    expected, _ = expected_image(original, old_path)
                    desired, extent = expected_image(original, args.image)
                    window = ((extent + 16383) // 16384) * 16384
                    prepare(target)
                    current = bytes(target.read_memory_block8(BASE, len(original)))
                    if current != expected:
                        raise RuntimeError('Full current MCU Flash differs from accepted previous image + original tail')
                    result['preflash_sha256'] = sha(current)
                    flash = target.memory_map.get_boot_memory().flash
                    flash.set_flash_algo_debug(True)
                    flash.init(flash.Operation.ERASE, reset=False)
                    try:
                        for offset in range(0, window, 16384):
                            flash.erase_sector(BASE + offset)
                    finally:
                        flash.uninit()
                    prepare(target)
                    flash.init(flash.Operation.PROGRAM, reset=False)
                    try:
                        for offset in range(0, window, 1024):
                            flash.program_page(BASE + offset, desired[offset:offset + 1024])
                    finally:
                        flash.uninit()
                    if bytes(target.read_memory_block8(BASE, len(original))) != desired:
                        raise RuntimeError('Full MCU readback mismatch')
                    result.update(readback_verified=True, original_tail_preserved=True,
                                  internal_flash_window_bytes=window, postflash_sha256=sha(desired))
                    target.set_vector_catch(Target.VectorCatch.NONE)
                    target.reset(Target.ResetType.SYSRESETREQ)
                    if target.get_state() == Target.State.HALTED:
                        target.resume()
                    time.sleep(.5)
                else:
                    # Inspect before issuing any RAM command or reset.
                    image = IntelHex(str(args.image))
                    expected = bytes(image.tobinarray(start=BASE, end=image.maxaddr()))
                    if bytes(target.read_memory_block8(BASE, len(expected))) != expected:
                        raise RuntimeError('Current firmware does not match S204 image')
                    if args.action == 'reset':
                        target.set_vector_catch(Target.VectorCatch.NONE)
                        target.reset(Target.ResetType.SYSRESETREQ)
                        if target.get_state() == Target.State.HALTED:
                            target.resume()
                        time.sleep(.5)

                if target.get_state() != Target.State.RUNNING:
                    raise RuntimeError('Target is not running; use explicit reset after verifying the image')

                def snapshot():
                    if target.read32(addresses['s204_command']):
                        raise RuntimeError('Diagnostic command still running')
                    data = list(target.read_memory_block32(addresses['s204_diag'], 32))
                    if data[0] & 1 or target.read32(addresses['s204_diag']) != data[0]:
                        raise RuntimeError('Diagnostic publication changed')
                    if target.read32(addresses['s204_fault']):
                        raise RuntimeError('Target fault')
                    if (target.read32(0x40020400) >> 12) & 3 or (target.read32(0x40020414) >> 6) & 1:
                        raise RuntimeError('PB6 is not input-low')
                    return data

                def command(number, timeout=15):
                    if target.read32(addresses['s204_command']):
                        raise RuntimeError('Command mailbox busy')
                    target.write32(addresses['s204_command'], number)
                    target.flush()
                    deadline = time.monotonic() + timeout
                    while target.read32(addresses['s204_command']):
                        if time.monotonic() > deadline:
                            raise TimeoutError('Command timed out: ' + str(number))
                        time.sleep(.002)
                    return snapshot()

                result['startup'] = snapshot()
                if result['startup'][2] != 0 or result['startup'][3] != 0xef4018:
                    raise RuntimeError('Flash init/JEDEC failed')
                if args.action == 'backup':
                    if not args.backup or args.backup.exists():
                        raise ValueError('A new backup path is required')
                    args.backup.parent.mkdir(parents=True, exist_ok=True)
                    digest = hashlib.sha256()
                    with args.backup.open('xb') as backup:
                        for offset in range(0, CAPACITY, 8192):
                            target.write32(addresses['s204_address'], offset)
                            target.write32(addresses['s204_length'], 8192)
                            diag = command(1)
                            if diag[1] != 0:
                                raise RuntimeError('Backup read failure')
                            data = bytes(target.read_memory_block8(addresses['s204_data'], 8192))
                            backup.write(data)
                            digest.update(data)
                            if offset % (1024 * 1024) == 0:
                                print('Backup MiB', offset // (1024 * 1024), flush=True)
                    if args.backup.stat().st_size != CAPACITY or sha(args.backup.read_bytes()) != digest.hexdigest():
                        raise RuntimeError('Backup file verify failed')
                    result.update(backup=str(args.backup.resolve()), backup_bytes=CAPACITY,
                                  backup_sha256=digest.hexdigest())
                elif args.action == 'provision':
                    if not args.backup_evidence:
                        raise ValueError('Successful full backup evidence required')
                    saved = json.loads(args.backup_evidence.read_text())
                    data = Path(saved['backup']).read_bytes()
                    if saved['status'] != 'PASS' or saved['uid'] != result['uid'] or len(data) != CAPACITY or sha(data) != saved['backup_sha256']:
                        raise ValueError('Backup evidence/identity/hash mismatch')
                    epoch = uuid.uuid4()
                    target.write_memory_block8(addresses['s204_data'], epoch.bytes)
                    diag = command(2, 60)
                    result['provisioned_epoch_hex'] = epoch.bytes.hex()
                    result['commands'].append({'number': 2, 'diag': diag})
                    if diag[1] != 0:
                        raise RuntimeError('Provision failed')
                elif args.action == 'commands':
                    for number in (int(x) for x in args.commands.split(',')):
                        if number not in range(3, 12):
                            raise ValueError('Use separate guarded provision/backup actions')
                        diag = command(number)
                        result['commands'].append({'number': number, 'diag': diag})
                        print(json.dumps(result['commands'][-1]), flush=True)
                        if number == 10:
                            from elftools.elf.elffile import ELFFile
                            with args.image.with_suffix('.elf').open('rb') as source:
                                size = ELFFile(source).get_section_by_name('.symtab').get_symbol_by_name('s204_page')[0]['st_size']
                            if not 0 < size <= 8192:
                                raise ValueError('Unexpected native page size')
                            result['commands'][-1]['page_native_hex'] = bytes(target.read_memory_block8(addresses['s204_page'], size)).hex()
                        if diag[1] != args.expected_result:
                            raise RuntimeError('Command failed')
                result['final'] = snapshot()
                result['target_state'] = target.get_state().name
                # Private SWD ABI: epoch[16], boot u64, watermark u64, loss u64.
                # This is an inspection of the ARM native DTO, never UART data.
                result['epoch_hex'] = bytes(target.read_memory_block8(addresses['s204_state'], 16)).hex()
                loss = target.read_memory_block32(addresses['s204_state'] + 32, 2)
                result['dropped_events'] = loss[0] | (loss[1] << 32)
                result['status'] = 'PASS'
        except Exception as error:
            result['status'] = 'FAIL'
            result['error'] = repr(error)
            raise
        finally:
            result['finished'] = datetime.now().astimezone().isoformat()
            evidence.write(json.dumps(result, indent=2) + '\n')
            evidence.flush()
            print('RESULT', result['status'], flush=True)


if __name__ == '__main__':
    main()
