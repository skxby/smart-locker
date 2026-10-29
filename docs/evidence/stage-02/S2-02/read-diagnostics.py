"""SWD snapshot of the exact accepted S2-02 image; never write memory/reset/flash.

--label records the user's physical setup, not a software-injected input.
Briefly halt/resume to read a coherent snapshot on the slow 100kHz SWD link.
"""
import argparse
import hashlib
import json
import re
from datetime import datetime
from pathlib import Path
from intelhex import IntelHex
from pyocd.core.helpers import ConnectHelper

ROOT = Path(__file__).resolve().parents[4]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--label', required=True)
parser.add_argument('--program-evidence', type=Path, default=Path(__file__).with_name('board-initial.json'))
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.output.exists():
    parser.error('Evidence already exists; use a new output')
prior = json.loads(args.program_evidence.read_text(encoding='utf-8'))
image_path = ROOT / 'build/firmware-s202/hse8_168/locker_s202.hex'
if prior.get('status') != 'PASS' or not prior.get('readback_verified'):
    raise SystemExit('No successful programming evidence')
if hashlib.sha256(image_path.read_bytes()).hexdigest() != prior['image_sha256']:
    raise SystemExit('Build changed since programming; use the corresponding evidence/build')
body = image_path.with_suffix('.map').read_text(encoding='utf-8', errors='replace')
def symbol(name):
    m = re.search(r'^\s*' + re.escape(name) + r'\s+(0x[0-9a-fA-F]+)\s+Data\s+', body, re.M)
    if not m:
        raise ValueError('Missing symbol ' + name)
    return int(m.group(1), 16)
options = {'target_override': 'stm32f407vgtx',
           'pack': 'E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0',
           'frequency': 100000, 'connect_mode': 'attach', 'auto_unlock': False,
           'resume_on_disconnect': False, 'pack.debug_sequences.enable': False}
session = ConnectHelper.session_with_chosen_probe(
    blocking=False, unique_id='000000000000000000000000', options=options)
if session is None:
    raise SystemExit('ST-LINK unavailable')
with session:
    target = session.target
    image = IntelHex(str(image_path))
    for start, end in image.segments():
        if bytes(target.read_memory_block8(start, end-start)) != bytes(image.tobinarray(start=start, end=end-1)):
            raise RuntimeError('Running Flash differs from accepted image')
    address = symbol('s202_diag')
    if target.get_state().name != 'RUNNING':
        raise RuntimeError('Target must already be running')
    for attempt in range(10):
        target.halt()
        try:
            data = target.read_memory_block32(address, 40)
        finally:
            target.resume()
        if not data[0] & 1:
            break
    else:
        raise RuntimeError('No coherent diagnostic snapshot')
    result = {'time': datetime.now().astimezone().isoformat(), 'physical_label': args.label,
              'image_sha256': prior['image_sha256'], 'state': target.get_state().name,
              'sampling': 'brief halt/resume; no memory writes',
              'fault': target.read32(symbol('s201_fault')), 'raw': data,
              'door_level': data[1], 'item_level': data[2],
              'door': data[3], 'item': data[4], 'supply_valid': bool(data[5]),
              'adc_code': data[6], 'supply_mv': data[7],
              'mono_ms': data[8] | (data[9] << 32),
              'rc522_init_status': data[10], 'rc522_version': hex(data[11]),
              'card_status': data[12], 'uid_length': data[13], 'uid': data[14:24][:data[13]],
              'flash_jedec': hex(data[24]), 'flash_status': hex(data[25]),
              'spi_transactions': data[26:28], 'spi_contentions': data[28], 'spi_errors': data[29],
              'card_max_ms': data[30], 'debounced_door': data[32], 'debounced_item': data[33],
              'spi_max_ms': data[34:36], 'card_observations': data[36],
              'gpiob_moder': hex(target.read32(0x40020400)),
              'gpiob_odr': hex(target.read32(0x40020414))}
    if result['fault'] != 0 or result['state'] != 'RUNNING':
        raise RuntimeError('Application not healthy')
args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(json.dumps(result, ensure_ascii=False))
