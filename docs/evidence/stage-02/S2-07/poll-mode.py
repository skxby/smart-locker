"""S2-07: high-frequency poll of MCU mode + freshness timers.

Watches when mode drops back to 0 and whether health_age/progress stall at
that moment. Offsets (DWARF-verified base 0x2000d7b0):
  +0x1c8 now(u64)  +0x1d8 progress(u64)  +0x1e0 progress_time(u64)
  +0x228 health_age(u32)  +0x22c mode(u8)
"""
import sys, time

sys.path.insert(0, 'C:/Users/%USER%/Documents/locker-hardware-tools/pyocd-0.46.0')
from pyocd.core.helpers import ConnectHelper

B = 0x2000d7b0
opts = {'target_override': 'stm32f407vgtx',
        'pack': 'E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0',
        'frequency': 950000, 'connect_mode': 'attach',
        'reset_type': 'sysresetreq', 'auto_unlock': False,
        'resume_on_disconnect': True, 'hide_programming_progress': True,
        'pack.debug_sequences.enable': False}

s = ConnectHelper.session_with_chosen_probe(blocking=False,
        unique_id='000000000000000000000000', options=opts)
with s:
    t = s.target
    t0 = time.time()
    end = t0 + float(sys.argv[1]) if len(sys.argv) > 1 else t0 + 45
    prev = None
    while time.time() < end:
        now = t.read32(B + 0x1e8) | (t.read32(B + 0x1ec) << 32)
        pt = t.read32(B + 0x200) | (t.read32(B + 0x204) << 32)
        prog = t.read32(B + 0x1f8)
        ha = t.read32(B + 0x228)
        mode = t.read8(B + 0x22c)
        cur = (mode, ha, prog, pt)
        if cur != prev:
            print('%.3f mode=%d health_age=%d progress=%d progress_age=%d'
                  % (time.time() - t0, mode, ha, prog, (now - pt) & 0xffffffffffff),
                  flush=True)
            prev = cur
        time.sleep(0.04)
print('POLL_DONE', flush=True)
