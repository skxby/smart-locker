"""S2-07: poll MCU control slots while the bench triggers a reconcile.

Reads (all offsets from mcu_control_t at 0x2000d7b0, verified via DWARF):
  +0x220 next_token (u32) - increments for every request AND event token
  +0x22c mode        (u8)  - 0=RECONCILING 1=BUSINESS
  +0x238 pending[16]       - each 424 bytes; token u32 @+0, kind u32 @+4
     kind enum: EVENT=1 RECOVER=2 SNAPSHOT=3 INTENT=4 FINALIZE=5 RELEASE=6 RESUME=7

Prints a line whenever (next_token, mode, occupied slots) changes.
"""
import sys, time

sys.path.insert(0, 'C:/Users/%USER%/Documents/locker-hardware-tools/pyocd-0.46.0')
from pyocd.core.helpers import ConnectHelper

BASE = 0x2000d7b0
NT, MODE, PEND = BASE + 0x220, BASE + 0x22c, BASE + 0x238
SLOT = 424
KINDS = {1: 'EVENT', 2: 'RECOVER', 3: 'SNAPSHOT', 4: 'INTENT',
         5: 'FINALIZE', 6: 'RELEASE', 7: 'RESUME'}

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
    end = t0 + float(sys.argv[1]) if len(sys.argv) > 1 else t0 + 40
    prev = None
    while time.time() < end:
        nt = t.read32(NT)
        mode = t.read8(MODE)
        buf = bytes(t.read_memory_block8(PEND, 16 * 8))
        slots = []
        for i in range(16):
            tok = int.from_bytes(buf[i*8:i*8+4], 'little')
            kind = int.from_bytes(buf[i*8+4:i*8+8], 'little')
            if tok:
                slots.append('%d:%s:0x%x' % (i, KINDS.get(kind, str(kind)), tok))
        cur = (nt, mode, tuple(slots))
        if cur != prev:
            print('%.3f nt=%d mode=%d %s' % (time.time() - t0, nt, mode,
                  ' '.join(slots) if slots else '-'), flush=True)
            prev = cur
        time.sleep(0.05)
print('POLL_DONE', flush=True)
