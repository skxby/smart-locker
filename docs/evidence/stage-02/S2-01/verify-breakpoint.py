"""Check an actual hardware breakpoint in the S2-01 safe HSE application."""
import json
import re
import time
from pathlib import Path
from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target

ROOT = Path(__file__).resolve().parents[4]
prior = json.loads(Path(__file__).with_name('board-run-hse8_168.json').read_text(encoding='utf-8'))
if prior.get('status') != 'PASS' or not prior.get('readback_verified'):
    raise SystemExit('HSE product image readback has not passed')
map_text = (ROOT / 'build/firmware-s201/hse8_168/locker_s201.map').read_text(encoding='utf-8', errors='replace')
match = re.search(r'^\s*TIM6_DAC_IRQHandler\s+(0x[0-9a-fA-F]+)\s+Thumb Code', map_text, re.M)
if not match:
    raise SystemExit('TIM6 handler symbol missing')
break_address = int(match.group(1), 16) & ~1
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
    target.halt()
    if not target.set_breakpoint(break_address, Target.BreakpointType.HW):
        raise RuntimeError('Hardware breakpoint could not be set')
    try:
        target.resume()
        for _ in range(30):
            time.sleep(0.05)
            if target.get_state() == Target.State.HALTED:
                break
        if target.get_state() != Target.State.HALTED:
            raise RuntimeError('Target did not stop at breakpoint')
        pc = target.read_core_register('pc') & ~1
        if pc != break_address:
            raise RuntimeError('Stopped at unexpected address: ' + hex(pc))
        print(json.dumps({'tim6_breakpoint': hex(break_address), 'pc': hex(pc),
                          'type': 'hardware', 'status': 'PASS'}))
    finally:
        target.remove_breakpoint(break_address)
        target.reset(Target.ResetType.SYSRESETREQ)
        target.resume()
