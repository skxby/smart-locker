"""Fetch pinned official MCU dependencies and snapshot the original HAL read-only."""
import hashlib
import io
import json
import shutil
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'firmware/third_party'
PINS = {
    'freertos': ('FreeRTOS/FreeRTOS-Kernel', 'V11.2.0', '0adc196d4bd52a2d91102b525b0aafc1e14a2386'),
    'littlefs': ('littlefs-project/littlefs', 'v2.11.2', 'adad0fbbcf5382c20978d07f94f9c13be9041c1b'),
}


def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'locker-firmware-environment'})
    with urllib.request.urlopen(request, timeout=45) as response:
        return response.read()


def main():
    lock_path = ROOT / 'deployment/firmware/dependencies.lock.json'
    if lock_path.exists():
        lock = json.loads(lock_path.read_text(encoding='utf-8'))
        for relative, expected in lock['files'].items():
            if hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() != expected:
                raise RuntimeError('Dependency changed; not overwriting: ' + relative)
        print('Pinned dependencies already present; all file hashes verified.')
        return
    lock = {'format_version': 1, 'dependencies': {}, 'files': {}}
    for name, (repo, tag, commit) in PINS.items():
        url = f'https://codeload.github.com/{repo}/tar.gz/{commit}'
        data = fetch(url)
        count = 0
        with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
            for member in archive.getmembers():
                if not member.isfile():
                    continue
                relative = Path(*Path(member.name).parts[1:])
                if '..' in relative.parts or relative.is_absolute():
                    raise RuntimeError('Invalid archive path')
                value = relative.as_posix()
                if name == 'freertos':
                    selected = value in ['tasks.c','queue.c','list.c','timers.c','event_groups.c','stream_buffer.c','croutine.c','LICENSE.md','portable/MemMang/heap_4.c'] or value.startswith(('include/', 'portable/RVDS/ARM_CM4F/', 'portable/GCC/ARM_CM4F/'))
                else:
                    selected = value in ['lfs.c','lfs.h','lfs_util.c','lfs_util.h','LICENSE.md']
                if selected:
                    target = DEST / name / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    content = archive.extractfile(member).read()
                    if target.exists() and target.read_bytes() != content:
                        raise RuntimeError('Existing dependency differs: ' + str(target))
                    target.write_bytes(content)
                    count += 1
        lock['dependencies'][name] = {'repository': 'https://github.com/' + repo, 'tag': tag, 'commit': commit, 'archive_sha256': hashlib.sha256(data).hexdigest(), 'files_selected': count, 'license': 'MIT' if name == 'freertos' else 'BSD-3-Clause'}
    original = ROOT / 'STM32F4xx_HAL_Code'
    for src_dir, dest_dir in [('STM32F4xx_HAL_Driver', 'stm32f4/hal'), ('CORE', 'stm32f4/cmsis')]:
        source = original / src_dir
        for path in source.rglob('*'):
            if path.is_file() and path.suffix.lower() in ['.h','.c','.s']:
                target = DEST / dest_dir / path.relative_to(source)
                target.parent.mkdir(parents=True, exist_ok=True)
                if target.exists() and target.read_bytes() != path.read_bytes():
                    raise RuntimeError('Existing HAL snapshot differs')
                shutil.copyfile(path, target)
    license_data = fetch('https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/v1.8.5/LICENSE.md')
    (DEST / 'stm32f4/hal/LICENSE.md').write_bytes(license_data)
    lock['dependencies']['stm32f4'] = {'origin': 'STM32F4xx_HAL_Code (local read-only snapshot)', 'hal_version': '1.8.5', 'cmsis_core': '5.6.0', 'cmsis_device': '2.6.11', 'license_note': 'Keep original source notices. HAL upstream BSD-3-Clause text supplied; CMSIS headers carry their own notices.'}
    for path in sorted(DEST.rglob('*')):
        if path.is_file():
            lock['files'][path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    lock_path.write_text(json.dumps(lock, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('Pinned dependency files:', len(lock['files']), '; original HAL unchanged.')


if __name__ == '__main__':
    main()
