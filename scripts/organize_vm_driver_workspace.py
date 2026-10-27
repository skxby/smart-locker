"""Non-destructive, hash-verified organization of the user's known driver experiments."""
import hashlib
import json
import os
import shutil
import sys
from datetime import datetime
from pathlib import Path


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    if sys.platform != 'linux':
        raise SystemExit('Run only inside the confirmed Ubuntu VM.')
    home = Path.home().resolve()
    root = home / 'kernel-driver-workspace'
    if root.is_symlink():
        raise SystemExit('Workspace root is a symlink')
    if (root / 'docs/import-manifest.json').exists():
        raise SystemExit('Import already exists; inspect its manifest instead of overwriting.')
    known = ['a2','a3audit','am-final','am-test2','am-test3','am-v3','am-v4','am-v4b','am-v4c',
        'amcheck-v5','amcheck-v5b','amcheck-v5c','amcheck-v5d','amcheck-v6','applytest',
        'ck','e2e','gen3probe','hwmon-test','p2','run-1','series','series-build','series-draft',
        'series-v2','series-v3','series-v4','series-v5','series-verify']
    sources = []
    for name in known:
        parent = home / name
        if parent.is_dir() and not parent.is_symlink():
            sources.extend(p for p in parent.rglob('*') if p.is_file() and not p.is_symlink())
    loose = [p for p in home.iterdir() if p.is_file() and not p.is_symlink() and p.suffix in ['.rs','.patch','.c','.h']]
    sources.extend(loose)
    total = sum(p.stat().st_size for p in sources)
    if total > 256 * 1024 * 1024 or shutil.disk_usage(home).free < total * 3 + 512 * 1024 * 1024:
        raise SystemExit('Import exceeds conservative space limit; inspect before proceeding.')
    for name in ['src/snapshots','patches','build','logs','artifacts','archive/original-snapshots','docs']:
        target = root / name
        if target.is_symlink() or root not in target.resolve().parents:
            raise SystemExit('Unsafe workspace child')
        target.mkdir(parents=True, exist_ok=True)
    records = []
    code_suffix = {'.rs','.c','.h','.py','.sh','.toml','.md','.txt','.json','.yaml','.yml'}
    for source in sources:
        relative = source.relative_to(home)
        backup = root / 'archive/original-snapshots' / relative
        if backup.exists():
            raise SystemExit('Existing backup, not overwriting: ' + str(backup))
        backup.parent.mkdir(parents=True, exist_ok=True)
        before = digest(source)
        shutil.copy2(source, backup)
        if digest(backup) != before or digest(source) != before:
            raise SystemExit('Source changed or copy failed: ' + str(source))
        parts = relative.parts
        curated = None
        if '.git' not in parts:
            if source.suffix == '.patch':
                curated = root / 'patches' / relative
            elif source.suffix in ['.log','.notes','.tsv']:
                curated = root / 'logs/legacy-import' / relative
            elif source.suffix in code_suffix or source.name in ['Makefile','Kbuild','Kconfig','LICENSE','COPYING']:
                curated = root / 'src/snapshots' / relative
        if curated:
            curated.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(backup, curated)
            if digest(curated) != before:
                raise SystemExit('Curated copy verification failed')
        records.append({'origin':str(source),'backup':str(backup.relative_to(root)),
            'curated':str(curated.relative_to(root)) if curated else None,'bytes':source.stat().st_size,'sha256':before})
    kernel = home / 'linux-rust'
    if kernel.is_dir():
        (root / 'kernel-tree').symlink_to(kernel, target_is_directory=True)
    manifest = {'created_at':datetime.now().astimezone().isoformat(),'source_directories':[n for n in known if (home/n).is_dir()],
        'originals_removed':False,'source_bytes':total,'files':records,'kernel_tree':str(kernel),
        'kernel_tree_copied_or_modified':False,'drivers_compiled_or_loaded':False,
        'unclassified_originals':['hhh','test','myproject'],'excluded':['model directories','Python environments','complete kernel source tree']}
    (root / 'docs/import-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (root / 'README.md').write_text('''# Kernel driver engineering workspace

This is a non-destructive, verified import of existing Rust hwmon/TMP108 experiments.

- src/snapshots/: source/recipe snapshots, grouped by original directory; no final version selected automatically.
- patches/: patch series, preserving original paths and sequence.
- logs/legacy-import/: existing experiment logs, not claimed as new validation.
- archive/original-snapshots/: exact original files, including Git metadata and historical binaries.
- build/, artifacts/: reserved for future isolated builds and releases.
- docs/import-manifest.json: origin/target/SHA256/byte mapping for every copied file.
- kernel-tree -> original /home/%USER%/linux-rust: reference only; not copied or modified.

Many snapshots are fragments of an in-tree kernel implementation, not standalone modules.
Do not run every Makefile or load an old .ko blindly. First select a reviewed patch series,
match its kernel revision/compiler/Module.symvers, then build in a separate output directory.
Original directories remain for rollback and old absolute-path references. Nothing was deleted.
The hhh/test/myproject directories are not classified as driver work and were not moved.
Kernel configuration changes, installation and reboot have not been performed.
''',encoding='utf-8')
    print(json.dumps({'status':'PASS','workspace':str(root),'directories_imported':len(manifest['source_directories']),
        'files_verified':len(records),'bytes_archived':total,'originals_removed':False,'drivers_compiled_or_loaded':False}))


if __name__ == '__main__':
    main()
