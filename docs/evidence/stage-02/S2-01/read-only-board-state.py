"""Read board identity and PB6 state through SWD without flashing or resetting."""
import json
from pyocd.core.helpers import ConnectHelper

options = {
    'target_override': 'stm32f407vgtx',
    'pack': 'E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0',
    'frequency': 100000,
    'connect_mode': 'attach',
    'auto_unlock': False,
    'resume_on_disconnect': False,
    'pack.debug_sequences.enable': False,
}
session = ConnectHelper.session_with_chosen_probe(
    blocking=False, unique_id='000000000000000000000000', options=options)
if session is None:
    raise SystemExit('Verified ST-LINK probe unavailable')
with session:
    target = session.target
    values = {
        'dbgmcu_idcode': hex(target.read32(0xE0042000)),
        'flash_kib': target.read16(0x1FFF7A22),
        'rcc_ahb1enr': hex(target.read32(0x40023830)),
        'gpiob_moder': hex(target.read32(0x40020400)),
        'gpiob_odr': hex(target.read32(0x40020414)),
        'gpiob_idr': hex(target.read32(0x40020410)),
        'state': target.get_state().name,
    }
    values['pb6_mode'] = (int(values['gpiob_moder'], 16) >> 12) & 3
    values['pb6_output_bit'] = (int(values['gpiob_odr'], 16) >> 6) & 1
    values['pb6_input_bit'] = (int(values['gpiob_idr'], 16) >> 6) & 1
    print(json.dumps(values, ensure_ascii=False))
