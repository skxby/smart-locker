"""Inject HAL millisecond rollover into the non-actuating S2-01 shell."""
import json
import re
import time
from pathlib import Path
from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target

ROOT = Path(__file__).resolve().parents[4]
EVIDENCE = Path(__file__).with_name('board-run.json')
MAP = ROOT / 'build/firmware-s201/hsi16/locker_s201.map'
prior = json.loads(EVIDENCE.read_text(encoding='utf-8'))
if prior.get('status') != 'PASS' or not prior.get('readback_verified'):
    raise SystemExit('S2-01 product image has not passed readback')
body = MAP.read_text(encoding='utf-8', errors='replace')
def symbol(name):
    match = re.search(r'^\s*' + re.escape(name) + r'\s+(0x[0-9a-fA-F]+)\s+Data\s+', body, re.M)
    if not match:
        raise ValueError(name)
    return int(match.group(1), 16)

options = {'target_override': 'stm32f407vgtx',
           'pack': 'E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0',
           'frequency': 100000, 'connect_mode': 'attach',
           'reset_type': 'sysresetreq', 'auto_unlock': False,
           'resume_on_disconnect': False, 'pack.debug_sequences.enable': False}
session = ConnectHelper.session_with_chosen_probe(
    blocking=False, unique_id='000000000000000000000000', options=options)
if session is None:
    raise SystemExit('Verified ST-LINK unavailable')
with session:
    target = session.target
    if target.read32(symbol('s201_fault')) != 0 or target.read32(symbol('SystemCoreClock')) != 16000000:
        raise RuntimeError('Unexpected product state')
    if (target.read32(0x40020400) >> 12) & 3:
        raise RuntimeError('PB6 no longer in input mode')
    target.halt()
    target.write32(symbol('tick_high'), 7)
    target.write32(symbol('uwTick'), 0xfffffff0)
    target.flush()
    target.resume()
    time.sleep(0.12)
    target.halt()
    high = target.read32(symbol('tick_high'))
    low = target.read32(symbol('uwTick'))
    observed = (high << 32) | low
    result = {'start_high': 7, 'start_low': 0xfffffff0,
              'end_high': high, 'end_low': low, 'combined_ms': observed,
              'pb6_mode': (target.read32(0x40020400) >> 12) & 3}
    # Reset the shell to restore normal HAL time after the injected rollover.
    target.reset(Target.ResetType.SYSRESETREQ)
    target.resume()
    if high != 8 or not 0 < low < 1000:
        raise RuntimeError('64-bit HAL tick did not extend across rollover: ' + str(result))
    print(json.dumps(result))
