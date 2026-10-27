#!/usr/bin/env bash
set -euo pipefail
# Move only the 10 root-owned legacy directories, never delete files.
test "$(id -un)" = china
python3 -B /home/%USER%/kernel-driver-workspace/docs/relocate_vm_driver_originals.py --resume
sudo -v
if [ -d /home/%USER%/a2 ]; then
  test ! -e /home/%USER%/kernel-driver-workspace/archive/home-originals/a2
  sudo mv -T -- /home/%USER%/a2 /home/%USER%/kernel-driver-workspace/archive/home-originals/a2
fi
if [ -d /home/%USER%/am-v4 ]; then
  test ! -e /home/%USER%/kernel-driver-workspace/archive/home-originals/am-v4
  sudo mv -T -- /home/%USER%/am-v4 /home/%USER%/kernel-driver-workspace/archive/home-originals/am-v4
fi
if [ -d /home/%USER%/am-v4b ]; then
  test ! -e /home/%USER%/kernel-driver-workspace/archive/home-originals/am-v4b
  sudo mv -T -- /home/%USER%/am-v4b /home/%USER%/kernel-driver-workspace/archive/home-originals/am-v4b
fi
if [ -d /home/%USER%/hwmon-test ]; then
  test ! -e /home/%USER%/kernel-driver-workspace/archive/home-originals/hwmon-test
  sudo mv -T -- /home/%USER%/hwmon-test /home/%USER%/kernel-driver-workspace/archive/home-originals/hwmon-test
fi
if [ -d /home/%USER%/series-build ]; then
  test ! -e /home/%USER%/kernel-driver-workspace/archive/home-originals/series-build
  sudo mv -T -- /home/%USER%/series-build /home/%USER%/kernel-driver-workspace/archive/home-originals/series-build
fi
if [ -d /home/%USER%/series-v2 ]; then
  test ! -e /home/%USER%/kernel-driver-workspace/archive/home-originals/series-v2
  sudo mv -T -- /home/%USER%/series-v2 /home/%USER%/kernel-driver-workspace/archive/home-originals/series-v2
fi
if [ -d /home/%USER%/series-v3 ]; then
  test ! -e /home/%USER%/kernel-driver-workspace/archive/home-originals/series-v3
  sudo mv -T -- /home/%USER%/series-v3 /home/%USER%/kernel-driver-workspace/archive/home-originals/series-v3
fi
if [ -d /home/%USER%/series-v4 ]; then
  test ! -e /home/%USER%/kernel-driver-workspace/archive/home-originals/series-v4
  sudo mv -T -- /home/%USER%/series-v4 /home/%USER%/kernel-driver-workspace/archive/home-originals/series-v4
fi
if [ -d /home/%USER%/series-v5 ]; then
  test ! -e /home/%USER%/kernel-driver-workspace/archive/home-originals/series-v5
  sudo mv -T -- /home/%USER%/series-v5 /home/%USER%/kernel-driver-workspace/archive/home-originals/series-v5
fi
if [ -d /home/%USER%/series-verify ]; then
  test ! -e /home/%USER%/kernel-driver-workspace/archive/home-originals/series-verify
  sudo mv -T -- /home/%USER%/series-verify /home/%USER%/kernel-driver-workspace/archive/home-originals/series-verify
fi
python3 -B /home/%USER%/kernel-driver-workspace/docs/relocate_vm_driver_originals.py --resume
