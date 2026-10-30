"""Sample the MCU UART request counters without halting the core.

Reads are plain AHB-AP accesses while the target runs, so the RTOS is not
disturbed; a halt would itself look like a link stall.
"""
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, 'C:/Users/%USER%/Documents/locker-hardware-tools/pyocd-0.46.0')
import os
os.environ.setdefault('PYOCD_USB_BACKEND', 'pyusb')
from pyocd.core.helpers import ConnectHelper                       # noqa: E402

OPTIONS = {'target_override': 'stm32f407vgtx',
           'pack': 'E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0',
           'frequency': 950000, 'connect_mode': 'attach', 'reset_type': 'sysresetreq',
           'auto_unlock': False, 'resume_on_disconnect': True,
           'hide_programming_progress': True, 'pack.debug_sequences.enable': False}
STATS = 0x20011F30
TICK = 0x20000088

session = ConnectHelper.session_with_chosen_probe(
    blocking=False, unique_id='000000000000000000000000', options=OPTIONS)
rows = []
start = time.time()
with session:
    target = session.target
    while time.time() - start < 120:
        try:
            rows.append({'t': round(time.time() - start, 1),
                         'hello': target.read32(STATS),
                         'execute': target.read32(STATS + 24),
                         'tick': target.read32(TICK)})
        except Exception as exc:                                # noqa: BLE001
            rows.append({'t': round(time.time() - start, 1), 'error': str(exc)[:40]})
        time.sleep(0.5)
Path('build/uart-trace.json').write_text(json.dumps(rows, indent=0), encoding='utf-8')
print('SAMPLER DONE rows=%d' % len(rows))
