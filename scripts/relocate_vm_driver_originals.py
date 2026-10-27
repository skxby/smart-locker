"""Relocate verified legacy experiments out of HOME; never delete user data.

Run in the confirmed VM. Without --apply, only validate and print the plan.
The original import manifest remains an immutable historical record.
"""
import argparse
import hashlib
import json
import os
import re
import sys
from datetime import datetime
from pathlib import Path


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def check_busy(targets):
    busy = []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit() or int(proc.name) == os.getpid():
            continue
        try:
            cwd = (proc / 'cwd').resolve()
            argv = (proc / 'cmdline').read_bytes().split(b'\0')
            paths = [cwd]
            for arg in argv:
                value = os.fsdecode(arg)
                if value and not value.startswith('-'):
                    paths.append(Path(value) if value.startswith('/') else cwd / value)
            paths += [p.resolve() for p in (proc / 'fd').iterdir()]
            if any(p == t or t in p.parents for p in paths for t in targets):
                busy.append(proc.name)
        except (OSError, RuntimeError):
            continue
    if busy:
        raise SystemExit('Selected files are in use by PID(s): ' + ','.join(busy))


def resume(root, home):
    journal = root / 'docs/home-relocation.json'
    report = json.loads(journal.read_text())
    destination = root / 'archive/home-originals'
    if root.is_symlink() or destination.is_symlink() or root not in destination.resolve().parents:
        raise SystemExit('Unsafe archive root')
    if digest(root / 'docs/import-manifest.json') != report['import_manifest_sha256']:
        raise SystemExit('Historical import manifest changed')
    rows = report['imported_files'] + report['additional_driver_files']
    check_busy({Path(m['origin']) for m in report['moves'] if Path(m['origin']).exists()})
    # Validate every source or already relocated file before continuing.
    for row in rows:
        source = Path(row['origin'])
        if home not in source.parents:
            raise SystemExit('Unsafe journal source')
        moved = destination / source.relative_to(home)
        actual = source if source.exists() else moved
        if actual.is_symlink() or digest(actual) != row['sha256']:
            raise SystemExit('Changed source/archive: ' + str(actual))
    report['blocked_moves'] = []
    for move in report['moves']:
        source, target = Path(move['origin']), Path(move['destination'])
        if source.parent != home or target != destination / source.name or source.is_symlink():
            raise SystemExit('Unsafe move in journal')
        if not source.exists():
            if not target.exists():
                raise SystemExit('Both source and destination missing')
            if move not in report['completed_moves']:
                report['completed_moves'].append(move)
            continue
        if target.exists():
            raise SystemExit('Both source and destination exist; inspect manually')
        # New files or symlinks cannot silently enter an old directory.
        if source.is_dir():
            actual = {p for p in source.rglob('*') if p.is_file() or p.is_symlink()}
            expected = {Path(r['origin']) for r in rows if source in Path(r['origin']).parents}
            if actual != expected or any(p.is_symlink() for p in actual):
                raise SystemExit('Directory changed since validation: ' + str(source))
        for row in rows:
            p = Path(row['origin'])
            if p == source or source in p.parents:
                if digest(p) != row['sha256']:
                    raise SystemExit('Source changed before move')
        try:
            source.rename(target)
            report['completed_moves'].append(move)
        except PermissionError:
            report['blocked_moves'].append(move)
        report['status'] = 'IN_PROGRESS'
        journal.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    relocated = 0
    for row in rows:
        source = Path(row['origin'])
        moved = destination / source.relative_to(home)
        actual = source if source.exists() else moved
        if digest(actual) != row['sha256']:
            raise SystemExit('Post-move hash mismatch')
        relocated += int(not source.exists())
    report['status'] = 'PARTIAL_PERMISSION_BLOCKED' if report['blocked_moves'] else 'PASS'
    report['verified_relocated_files'] = relocated
    report['verified_total_files'] = len(rows)
    journal.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    readme = root / 'README.md'
    text = readme.read_text()
    text = re.sub(r'\d+ entries \(\d+ files\) have been relocated and hash-verified; \d+ root-owned directories remain permission-blocked\.',
                  f"{len(report['completed_moves'])} entries ({relocated} files) have been relocated and hash-verified; "
                  f"{len(report['blocked_moves'])} root-owned directories remain permission-blocked.", text)
    readme.write_text(text)
    print(json.dumps({k: v for k, v in report.items() if not isinstance(v, list)}))
    print(json.dumps({'completed_entries': len(report['completed_moves']),
                      'blocked_entries': [m['origin'] for m in report['blocked_moves']]}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--resume', action='store_true')
    args = parser.parse_args()
    if sys.platform != 'linux' or Path.home().resolve() != Path('/home/%USER%'):
        raise SystemExit('Run only as china inside the confirmed Ubuntu VM.')
    home = Path.home().resolve()
    root = home / 'kernel-driver-workspace'
    if root.is_symlink():
        raise SystemExit('Workspace must not be a symlink')
    if args.resume:
        resume(root, home)
        return
    destination = root / 'archive/home-originals'
    if destination.exists() or destination.is_symlink():
        raise SystemExit('Relocation already exists; inspect the journal, do not repeat.')
    manifest_path = root / 'docs/import-manifest.json'
    manifest = json.loads(manifest_path.read_text())
    records = manifest['files']
    expected = {Path(r['origin']): r for r in records}
    targets = {home / name for name in manifest['source_directories']}
    targets.update(p for p in expected if p.parent == home)
    for parent in targets:
        if parent.parent != home or parent.is_symlink() or not parent.exists():
            raise SystemExit('Unsafe/missing original: ' + str(parent))
        if parent.is_dir():
            actual = set()
            for current, dirs, files in os.walk(parent, followlinks=False):
                for name in dirs + files:
                    path = Path(current) / name
                    if path.is_symlink():
                        raise SystemExit('Unmanifested symlink: ' + str(path))
                actual.update(Path(current) / name for name in files)
            wanted = {p for p in expected if parent in p.parents}
            if actual != wanted:
                raise SystemExit('Directory changed since import: ' + str(parent))
    verified = []
    for path, row in expected.items():
        if home not in path.parents:
            raise SystemExit('Source escapes HOME')
        for candidate in [path, root / row['backup']] + ([root / row['curated']] if row['curated'] else []):
            if candidate.is_symlink() or not candidate.is_file() or digest(candidate) != row['sha256']:
                raise SystemExit('Changed/missing copy: ' + str(candidate))
        verified.append({'origin': str(path), 'sha256': row['sha256'], 'bytes': row['bytes']})

    # Only direct, identifiable driver references qualify; no generic "kernel"
    # keyword which could accidentally select model/package/network logs.
    protected_text = []
    for p in [home / '.bashrc', home / '.profile', home / '.zshrc']:
        if p.is_file():
            protected_text.append(p.read_text(errors='replace'))
    for folder in [home / '.config/systemd/user', Path('/etc/systemd/system')]:
        if folder.exists():
            for p in folder.rglob('*'):
                if p.is_file() and p.stat().st_size < 1024 * 1024:
                    try:
                        protected_text.append(p.read_text(errors='replace'))
                    except OSError:
                        pass
    extra = []
    for path in sorted(home.iterdir()):
        if (not path.is_file() or path.is_symlink() or path.name.startswith('.')
                or path in expected or path.suffix not in {'.sh', '.py', '.log', '.txt', '.out', '.diff'}
                or path.stat().st_size > 20 * 1024 * 1024):
            continue
        content = path.read_text(errors='replace')
        if not re.search(r'linux-rust|hwmon|tmp108|rust_driver', content):
            continue
        if any(path.name in text for text in protected_text):
            continue
        extra.append({'origin': str(path), 'sha256': digest(path), 'bytes': path.stat().st_size})
        targets.add(path)

    # Refuse to relocate an experiment in use, including open files or script argv.
    check_busy(targets)
    moves = []
    for path in sorted(targets):
        target = destination / path.name
        if target.parent != destination or root not in target.resolve().parents:
            raise SystemExit('Unsafe archive destination')
        moves.append({'origin': str(path), 'destination': str(target)})
    report = {'created_at': datetime.now().astimezone().isoformat(),
              'status': 'VALIDATED', 'import_manifest_sha256': digest(manifest_path),
              'moves': moves, 'imported_files': verified, 'additional_driver_files': extra,
              'imported_file_count': len(verified), 'additional_file_count': len(extra),
              'directory_count': len(manifest['source_directories']),
              'permanently_deleted': False, 'completed_moves': []}
    if not args.apply:
        print(json.dumps({k: v for k, v in report.items() if not isinstance(v, list)}))
        return
    journal = root / 'docs/home-relocation.json'
    if journal.exists():
        raise SystemExit('Existing relocation journal; inspect instead of overwriting')
    destination.mkdir(parents=True)
    journal.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    rows = verified + extra
    for move in moves:
        source = Path(move['origin'])
        for row in rows:
            path = Path(row['origin'])
            if path == source or source in path.parents:
                if digest(path) != row['sha256']:
                    raise SystemExit('Changed immediately before move: ' + str(path))
        source.rename(move['destination'])
        report['completed_moves'].append(move)
        journal.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    for row in rows:
        original = Path(row['origin'])
        moved = destination / original.relative_to(home)
        if original.exists() or digest(moved) != row['sha256']:
            raise SystemExit('Post-move verification failed: ' + str(original))
    report['status'] = 'PASS'
    report['verified_relocated_files'] = len(rows)
    journal.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    readme = root / 'README.md'
    text = readme.read_text()
    text = text.replace('This is a non-destructive, verified import of existing Rust hwmon/TMP108 experiments.',
                        'This workspace contains verified Rust hwmon/TMP108 experiments and their relocated originals.')
    text = text.replace('Original directories remain for rollback and old absolute-path references. Nothing was deleted.',
                        'Original HOME entries are relocated to archive/home-originals/. See docs/home-relocation.json.\n'
                        'Nothing was permanently deleted. Legacy scripts with absolute HOME paths require review before reuse.\n'
                        'import-manifest.json records historical pre-relocation paths, not current file locations.')
    readme.write_text(text)
    print(json.dumps({k: v for k, v in report.items() if not isinstance(v, list)}))


if __name__ == '__main__':
    main()
