"""Recover the existing ST-LINK/V2 USB command channel without target writes."""
import json
import time
import usb.util
from libusb_package import find


def get_device():
    device = find(idVendor=0x0483, idProduct=0x3748)
    if device is None:
        raise RuntimeError('ST-LINK/V2 USB device not found')
    return device


device = get_device()
device.reset()
usb.util.dispose_resources(device)
time.sleep(1.0)
device = get_device()
try:
    usb.util.claim_interface(device, 0)
    device.clear_halt(0x02)
    device.clear_halt(0x81)

    def command(data, count):
        packet = bytes(data).ljust(16, b'\0')
        device.write(0x02, packet, timeout=2000)
        return bytes(device.read(0x81, count, timeout=2000)) if count else b''

    version = command([0xF1], 6)
    mode_before = command([0xF5], 2)[0]
    if mode_before == 0:
        command([0xF3, 0x07], 0)  # Normal exit from ST-LINK DFU mode; no upgrade.
    mode_after = command([0xF5], 2)[0]
    print(json.dumps({'version_hex': version.hex(), 'mode_before': mode_before,
                      'mode_after': mode_after, 'target_flash_modified': False}))
finally:
    usb.util.release_interface(device, 0)
    usb.util.dispose_resources(device)
