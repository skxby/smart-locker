"""Read-only SWD snapshot after real RK UART tests; no memory injection/reset."""
import argparse
import hashlib
import json
import re
from pathlib import Path
from datetime import datetime
from intelhex import IntelHex
from pyocd.core.helpers import ConnectHelper

ROOT = Path(__file__).resolve().parents[4]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--program-evidence', type=Path, default=Path(__file__).with_name('board-uart-startup-02.json'))
parser.add_argument('--uart-result', type=Path, default=Path(__file__).with_name('uart-real-01.json'))
args = parser.parse_args()
if args.output.exists():
    parser.error('Evidence exists; select a new output')
prior = json.loads(args.program_evidence.read_text(encoding='utf-8'))
image_path = Path(prior['image'])
if prior['status'] != 'PASS' or not prior['readback_verified']:
    raise SystemExit('No successful programming evidence')
if hashlib.sha256(image_path.read_bytes()).hexdigest() != prior['image_sha256']:
    raise SystemExit('Build changed since programming')
body = image_path.with_suffix('.map').read_text(encoding='utf-8', errors='replace')

def address(name):
    match = re.search(r'^\s*'+re.escape(name)+r'\s+(0x[0-9a-fA-F]+)\s+Data\s+', body, re.M)
    if not match:
        raise ValueError('Missing symbol '+name)
    return int(match.group(1), 16)

options = {'target_override': 'stm32f407vgtx',
           'pack': 'E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0',
           'frequency': 100000, 'connect_mode': 'attach',
           'auto_unlock': False, 'resume_on_disconnect': False,
           'pack.debug_sequences.enable': False}
session = ConnectHelper.session_with_chosen_probe(blocking=False,
    unique_id='000000000000000000000000', options=options)
if session is None:
    raise SystemExit('ST-LINK unavailable')
result = {'at': datetime.now().astimezone().isoformat(), 'status': 'FAIL',
          'operation': 'halt/read/resume only; no reset or memory write'}
try:
    with session:
        target = session.target
        if target.get_state().name != 'RUNNING':
            raise RuntimeError('Target not running; do not change its state')
        target.halt()
        try:
            image = IntelHex(str(image_path))
            expected = bytes(image.tobinarray(start=0x08000000, end=image.maxaddr()))
            if bytes(target.read_memory_block8(0x08000000, len(expected))) != expected:
                raise RuntimeError('Current Flash does not match tested S203 HEX')
            result['image_bytes_match'] = True
            result['image_sha256'] = prior['image_sha256']
            result['uid_hex'] = bytes(target.read_memory_block8(0x1fff7a10, 12)).hex()
            result['diag'] = list(target.read_memory_block32(address('s203_diag'), 24))
            for name in ['s201_fault', 's201_safety_checks', 'uwTick', 'xTickCount']:
                result[name] = target.read32(address(name))
            for name, register in {
                'gpiob_moder': 0x40020400, 'gpiob_odr': 0x40020414,
                'gpiod_moder': 0x40020c00, 'gpiod_afrl': 0x40020c20,
                'dma1_stream5_ndtr': 0x4002608c,
                'dma1_stream6_ndtr': 0x400260a4,
                'usart2_sr': 0x40004400}.items():
                result[name] = target.read32(register)
        finally:
            target.resume()
    uart = json.loads(args.uart_result.read_text(encoding='utf-8'))
    if uart['status'] != 'PASS':
        raise RuntimeError('UART acceptance result not PASS')
    uid = next(c for c in uart['checks'] if c['name'] == 'HELLO_diagnostic')['details']['uid_hex']
    if result['uid_hex'] != uid:
        raise RuntimeError('Wire UID differs from chip UID')
    diag = result['diag']
    if result['s201_fault'] != 0 or result['s201_safety_checks'] != 15 or diag[1] != 1:
        raise RuntimeError('Board no longer in restricted running state')
    if ((result['gpiob_moder'] >> 12) & 3) or ((result['gpiob_odr'] >> 6) & 1):
        raise RuntimeError('PB6 lock output not input-low')
    if ((result['gpiod_moder'] >> 10) & 15) != 10:
        raise RuntimeError('PD5/PD6 not both alternate function')
    if ((result['gpiod_afrl'] >> 20) & 255) != 0x77:
        raise RuntimeError('PD5/PD6 not both USART2 AF7')
    total_tx = sum(chunk['length'] for chunk in uart['raw_io'] if chunk['direction'] == 'TX')
    result['rk_transmitted_bytes'] = total_tx
    result['wire_uid_matches_chip'] = True
    result['tx_full_matches_missing_replies'] = diag[13] == next(
        c for c in uart['checks'] if c['name'] == 'bounded_TX_burst')['details']['sent_count'] - next(
        c for c in uart['checks'] if c['name'] == 'bounded_TX_burst')['details']['response_count']
    if diag[7] != total_tx or diag[5] != 0 or diag[6] != 0:
        raise RuntimeError('RX byte loss, HAL error or scheduler/DMA overrun observed')
    if diag[10] < 1 or diag[11] < 1 or diag[12] < 1 or diag[13] < 1:
        raise RuntimeError('Expected CRC/length/session/TX-backpressure counters absent')
    if not result['tx_full_matches_missing_replies']:
        raise RuntimeError('Reply loss does not match MCU TX-full count')
    result['status'] = 'PASS'
finally:
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
