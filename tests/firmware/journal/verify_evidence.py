"""Read-only checks of S2-04 recorded board evidence; never drives hardware."""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--elf', type=Path, required=True)
    args = parser.parse_args()

    def read(name, status='PASS'):
        value = json.loads((args.evidence / name).read_text(encoding='utf-8'))
        assert value['status'] == status, (name, value['status'])
        return value

    program = read('board-program-03.json', 'READBACK_PASS_RUNTIME_NOT_CONFIRMED')
    assert program['readback_verified'] and program['original_tail_preserved']
    startup = read('board-startup-final.json')
    assert startup['target_state'] == 'RUNNING'
    backup = read('flash-backup-01.json')
    raw = Path(backup['backup']).read_bytes()
    assert len(raw) == 16777216 and hashlib.sha256(raw).hexdigest() == backup['backup_sha256']
    assert backup['uid'] == startup['uid'] and startup['final'][3] == 0xef4018
    uninitialized = read('mount-uninitialized-final.json')
    assert uninitialized['commands'][0]['diag'][1] == 8 and uninitialized['final'][4] == 0
    provision = read('provision-01.json')
    before = read('transactions-before-cut.json')
    after = read('recovery-after-powercut.json')
    terminal = read('finalize-release-clean-reboot.json')
    clean = read('recovery-after-clean-reset.json')
    stopped = read('final-safe-close.json')
    operator = json.loads((args.evidence / 'operator-powercycle.json').read_text(encoding='utf-8'))
    assert operator['mcu_power_led_extinguished_reported']

    def u64(diag, offset):
        return diag[offset] | (diag[offset + 1] << 32)

    a, b, c = before['final'], after['final'], clean['final']
    assert u64(a, 5) == 1 and u64(b, 5) == 2 and u64(c, 5) == 4
    assert all(u64(diag, 7) == 1 for diag in [a, b, c])
    assert (a[9], a[10], a[11], a[29], a[30]) == (1, 1, 3, 1, 1)
    assert (b[9], b[10], b[11], b[29], b[30]) == (1, 5, 4, 2, 0)
    assert (c[9], c[10], c[11], c[29], c[30]) == (0, 0, 7, 2, 0)
    assert all(u64(diag, 20) == 1 and u64(diag, 22) == 3 for diag in [a, b, c])
    assert all(u64(diag, 12) == 1 and u64(diag, 14) == 1 for diag in [a, b, c])
    assert before['dropped_events'] == 0
    assert after['dropped_events'] == clean['dropped_events'] == (1 << 64) - 1
    assert before['epoch_hex'] == after['epoch_hex'] == clean['epoch_hex'] == provision['provisioned_epoch_hex']
    for name in ['degraded-intent-rejected.json', 'release-does-not-clear-fault.json']:
        rejected = read(name)
        assert rejected['commands'][0]['diag'][1] == 5 and u64(rejected['final'], 7) == 1
    assert terminal['commands'][0]['diag'][10] == 4  # Persisted known-not-driven terminal.
    assert terminal['commands'][1]['diag'][9] == 0  # Persisted release.
    assert stopped['final'][4] == 0 and stopped['target_state'] == 'RUNNING'

    # Use the target compiler's DWARF layout, not a copied native byte layout.
    from elftools.elf.elffile import ELFFile
    with args.elf.open('rb') as source:
        dwarf = ELFFile(source).get_dwarf_info()
        types = {}
        for cu in dwarf.iter_CUs():
            for die in cu.iter_DIEs():
                name = die.attributes.get('DW_AT_name')
                if die.tag == 'DW_TAG_typedef' and name and name.value in [b'locker_device_event_t', b'locker_event_batch_t']:
                    types[name.value] = die.get_DIE_from_attribute('DW_AT_type')
        event_size = types[b'locker_device_event_t'].attributes['DW_AT_byte_size'].value
        fields = {die.attributes['DW_AT_name'].value: die.attributes['DW_AT_data_member_location'].value
                  for die in types[b'locker_event_batch_t'].iter_children() if die.tag == 'DW_TAG_member'}
    page_before = bytes.fromhex(next(cmd['page_native_hex'] for cmd in before['commands'] if cmd['number'] == 10))
    page_after = bytes.fromhex(next(cmd['page_native_hex'] for cmd in after['commands'] if cmd['number'] == 10))
    assert page_before[fields[b'count']] == 3 and page_after[fields[b'count']] == 4
    begin = fields[b'events']
    assert page_before[begin:begin + 3 * event_size] == page_after[begin:begin + 3 * event_size]
    print(json.dumps({'status': 'PASS', 'flash_bytes_backed_up': len(raw),
        'same_uid_and_epoch': True, 'original_three_events_byte_identical': True,
        'physical_power_cut_position': 'after committed intent/watermark/ACK',
        'clean_and_unclean_recovery_checked': True, 'fault_latch_not_cleared': True,
        'lock_hard_cutoff_tested': False, 'physical_mid_program_power_cut_tested': False}))


if __name__ == '__main__':
    main()
