"""Fixed-source VMware validation. Creates a new batch; never replaces old sources."""
import argparse
import hashlib
import io
import json
import re
import subprocess
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', required=True)
    parser.add_argument('--sanitizers', action='store_true')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.run):
        parser.error('Unsafe run identifier')
    output = ROOT / 'build/vm-takeover'
    output.mkdir(parents=True, exist_ok=True)
    archive = output / (args.run + '.tar.gz')
    if archive.exists():
        parser.error('Archive already exists; use a new run identifier')
    files = [ROOT / 'CMakeLists.txt', ROOT / 'scripts/check_network_dependencies.py', ROOT / 'scripts/check_contracts.py', ROOT / 'scripts/build_contracts.py']
    for name in ['host', 'tests', 'contracts', 'deployment/host']:
        files.extend(p for p in (ROOT / name).rglob('*')
                     if p.is_file() and '__pycache__' not in p.parts)
    manifest = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(files)}
    with tarfile.open(archive, 'w:gz') as tar:
        for path in sorted(files):
            tar.add(path, arcname=path.relative_to(ROOT).as_posix(), recursive=False)
        data = (json.dumps(manifest, indent=2) + '\n').encode()
        entry = tarfile.TarInfo('snapshot-manifest.json')
        entry.size = len(data)
        tar.addfile(entry, io.BytesIO(data))
    print('SNAPSHOT', args.run, 'FILES', len(files), 'SHA256',
          hashlib.sha256(archive.read_bytes()).hexdigest(), flush=True)
    subprocess.run(['ssh', '-o', 'BatchMode=yes', 'locker-vm',
                    'mkdir -p "$HOME/locker-workspace/artifacts/S1-05"'], check=True)
    subprocess.run(['scp', str(archive),
                    f'locker-vm:locker-workspace/artifacts/S1-05/{args.run}.tar.gz'], check=True)
    script = '''set -eu
workspace="$HOME/locker-workspace"
batch="$workspace/S1-05/batches/RUNID"
logs="$workspace/logs/S1-05/RUNID"
test ! -e "$batch"
mkdir -p "$batch/src" "$logs"
tar -xf "$workspace/artifacts/S1-05/RUNID.tar.gz" -C "$batch/src"
python3 - "$batch/src" <<'VERIFY'
from pathlib import Path
import json, hashlib, sys
root = Path(sys.argv[1])
manifest = json.loads((root/'snapshot-manifest.json').read_text())
assert all(hashlib.sha256((root/name).read_bytes()).hexdigest() == digest
           for name, digest in manifest.items())
print('VERIFIED_FILES', len(manifest))
VERIFY
{
    date -Iseconds
    uname -a
    cmake --version | head -1
    gcc --version | head -1
} > "$logs/environment.log"
run_tree() {
    name="$1"
    flags="$2"
    build="$batch/build-$name"
    cmake -S "$batch/src" -B "$build" -DCMAKE_BUILD_TYPE=Debug -DLOCKER_BUILD_TESTS=ON -DLOCKER_BUILD_NETWORK=ON -DLOCKER_WARNINGS_AS_ERRORS=ON -DCMAKE_C_FLAGS="$flags" > "$logs/$name-configure.log" 2>&1 || { tail -60 "$logs/$name-configure.log"; return 1; }
    cmake --build "$build" -j2 > "$logs/$name-build.log" 2>&1 || { tail -70 "$logs/$name-build.log"; return 1; }
    ctest --test-dir "$build" --output-on-failure > "$logs/$name-ctest.log" 2>&1 || { cat "$logs/$name-ctest.log"; return 1; }
    echo "$name CONFIGURE=0 BUILD=0 CTEST=0"
    tail -5 "$logs/$name-ctest.log"
}
run_tree debug ""
SANITIZERS
date -Iseconds >> "$logs/environment.log"
'''.replace('RUNID', args.run)
    sanitizer_commands = '''run_tree asan "-fsanitize=address,undefined -fno-omit-frame-pointer -g"
run_tree tsan "-fsanitize=thread -fno-omit-frame-pointer -g"'''
    script = script.replace('SANITIZERS', sanitizer_commands if args.sanitizers else '')
    result = subprocess.run(['ssh', '-o', 'BatchMode=yes', 'locker-vm', 'bash', '-s'],
                            input=script.encode())
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
