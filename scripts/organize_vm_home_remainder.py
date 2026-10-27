"""Archive inventoried HOME loose files and five inactive legacy directories.

No deletion, dependency installation or source editing. Run --apply only in the
confirmed VM as china. The journal supports restarting an interrupted move.
"""
import argparse
import hashlib
import json
import os
import re
import stat
import subprocess
import sys
from datetime import datetime
from pathlib import Path


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inventory(path):
    rows = []
    paths = [path]
    if path.is_dir() and not path.is_symlink():
        for current, dirs, files in os.walk(path, followlinks=False):
            paths.extend(Path(current) / n for n in dirs + files)
    for p in paths:
        s = p.lstat()
        kind = ('file' if stat.S_ISREG(s.st_mode) else 'directory' if stat.S_ISDIR(s.st_mode)
                else 'symlink' if stat.S_ISLNK(s.st_mode) else 'special')
        rows.append({'relative': str(p.relative_to(path)) if p != path else '.',
                     'kind': kind, 'mode': stat.S_IMODE(s.st_mode), 'uid': s.st_uid,
                     'gid': s.st_gid, 'bytes': s.st_size, 'mtime_ns': s.st_mtime_ns,
                     'sha256': sha(p) if kind == 'file' else None,
                     'link': os.readlink(p) if kind == 'symlink' else None,
                     'rdev': s.st_rdev if kind == 'special' else None})
    return sorted(rows, key=lambda r: r['relative'])


def assert_unused(targets):
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit() or int(proc.name) == os.getpid():
            continue
        try:
            cwd = (proc / 'cwd').resolve()
            args = (proc / 'cmdline').read_bytes().split(b'\0')
            paths = [cwd] + [p.resolve() for p in (proc / 'fd').iterdir()]
            for raw in args:
                arg = os.fsdecode(raw)
                if arg and not arg.startswith('-'):
                    paths.append(Path(arg) if arg.startswith('/') else cwd / arg)
            if any(p == t or t in p.parents for t in targets for p in paths):
                raise SystemExit('Target is in use by PID ' + proc.name)
        except (OSError, RuntimeError):
            continue


def assert_not_automatically_referenced(home, files):
    configs = [home / n for n in ['.bashrc', '.profile', '.zshrc', '.ssh/config']]
    for folder in [home / '.config/systemd/user', Path('/etc/systemd/system'),
                   Path('/etc/cron.d'), Path('/etc/profile.d')]:
        if folder.is_dir():
            configs.extend(p for p in folder.rglob('*') if p.is_file() and p.stat().st_size < 1024 * 1024)
    texts = []
    for p in configs:
        try:
            texts.append((str(p), p.read_text(errors='replace')))
        except OSError:
            continue
    cron = subprocess.run(['crontab', '-l'], capture_output=True, text=True)
    if cron.returncode == 0:
        texts.append(('user crontab', cron.stdout))
    for source in files:
        # Token boundaries avoid confusing up.sh with setup.sh.
        pattern = r'(?<![\w./-])' + re.escape(source.name) + r'(?![\w.-])'
        for location, text in texts:
            if str(source) in text or re.search(pattern, text):
                raise SystemExit(f'Configured automatic reference: {location} -> {source.name}')


def destination_for(home, path):
    n = path.name
    projects = home / 'Projects'
    if n in ['hhh', 'myproject', 'test', 'xyx.git']:
        return projects / 'legacy-learning/originals' / n
    if n == 'initramfs':
        return home / 'kernel-driver-workspace/archive/home-supplement' / n
    if (path.suffix in {'.onnx', '.gguf', '.tgz'} or n.startswith('day29_bge')
            or n == 'requirements_cp38-1.6.0.txt' or n == 'vllm.log'):
        return projects / 'ai-lab/imported-assets' / n
    if path.suffix in {'.deb', '.whl'} or n in ['rustup-init-bin', 'rustup.sh']:
        return projects / 'tools/installers' / n
    if (n in ['99proxy', 'ubuntu.sources', '_wb-key.pub']
            or re.search(r'^(apt|net-|_vm-nic|_vm-proxy|_vm-resize|sshkey|grub|verify-grub|boot-check|osdiag|sata|postfix|emerg|newkernel)', n)):
        return projects / 'system-maintenance/originals' / n
    return home / 'kernel-driver-workspace/archive/home-supplement' / n


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    home = Path.home().resolve()
    if sys.platform != 'linux' or home != Path('/home/%USER%'):
        raise SystemExit('Run only as china inside the confirmed VM.')
    projects = home / 'Projects'
    if projects.is_symlink() or not projects.is_dir():
        raise SystemExit('Expected real Projects directory')
    journal = projects / 'home-organization.json'
    if journal.exists():
        report = json.loads(journal.read_text())
    else:
        loose = sorted(p for p in home.iterdir() if not p.name.startswith('.') and p.is_file() and not p.is_symlink())
        directories = [home / n for n in ['hhh', 'myproject', 'test', 'xyx.git', 'initramfs'] if (home / n).exists()]
        assert_not_automatically_referenced(home, loose)
        assert_unused(set(loose + directories))
        moves = []
        for source in loose + directories:
            target = destination_for(home, source)
            if source.parent != home or source.is_symlink() or home not in target.resolve().parents:
                raise SystemExit('Unsafe source/destination')
            if target.exists() or target.is_symlink():
                raise SystemExit('Destination exists: ' + str(target))
            # Validate each existing parent, not just the final path.
            for parent in target.parents:
                if parent == home:
                    break
                if parent.is_symlink():
                    raise SystemExit('Destination ancestor is a symlink: ' + str(parent))
            moves.append({'origin': str(source), 'destination': str(target),
                          'inventory': inventory(source), 'completed': False})
        report = {'created_at': datetime.now().astimezone().isoformat(), 'status': 'VALIDATED',
                  'moves': moves, 'permanently_deleted': False,
                  'loose_file_count': len(loose), 'legacy_directory_count': len(directories),
                  'preserved_paths': ['linux-rust', 'aarch64-linux-musl-cross', 'proj101env',
                                      'rknn-env', 'rknn-env152', 'tf4env', 'vllm-env',
                                      'bge_model', 'bge_work', 'models', 'llama.cpp-src',
                                      'locker-workspace', 'kernel-driver-workspace', 'snap']}
    if not args.apply:
        print(json.dumps({k: v for k, v in report.items() if k != 'moves'}))
        print(json.dumps({'move_count': len(report['moves']),
                          'regular_files': sum(r['kind'] == 'file' for m in report['moves'] for r in m['inventory']),
                          'bytes': sum(r['bytes'] for m in report['moves'] for r in m['inventory'] if r['kind'] == 'file')}))
        return
    assert_unused({Path(m['origin']) for m in report['moves'] if Path(m['origin']).exists()})
    # Recheck all sources before the first mutation.
    for move in report['moves']:
        source, target = Path(move['origin']), Path(move['destination'])
        if source.parent != home or target != destination_for(home, source):
            raise SystemExit('Unsafe journal path')
        actual = source if source.exists() else target
        if inventory(actual) != move['inventory']:
            raise SystemExit('Source/archive changed: ' + str(actual))
    journal.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    for move in report['moves']:
        source, target = Path(move['origin']), Path(move['destination'])
        if source.exists():
            if target.exists():
                raise SystemExit('Both paths exist; inspect before resuming')
            if inventory(source) != move['inventory']:
                raise SystemExit('Changed before move')
            target.parent.mkdir(parents=True, exist_ok=True)
            source.rename(target)
        if inventory(target) != move['inventory']:
            raise SystemExit('Post-move verification failed: ' + str(target))
        move['completed'] = True
        report['status'] = 'IN_PROGRESS'
        journal.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    report['status'] = 'PASS'
    report['remaining_visible_entries'] = sorted(p.name for p in home.iterdir() if not p.name.startswith('.'))
    report['remaining_visible_loose_files'] = [p.name for p in home.iterdir() if p.is_file() and not p.name.startswith('.')]
    journal.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'moves'}, ensure_ascii=False))


if __name__ == '__main__':
    main()
