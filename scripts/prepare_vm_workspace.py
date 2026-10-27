"""Create a separate, non-destructive Linux VM workspace; never move sources."""
import argparse
import json
import sys
from pathlib import Path


def layout_paths(root, current_source):
    root = root.expanduser().resolve()
    current_source = current_source.expanduser().resolve()
    if root == current_source or current_source in root.parents:
        raise ValueError('Workspace must be outside the current source directory')
    names = ['src', 'build', 'runtime/service', 'runtime/simulator', 'logs', 'artifacts']
    paths = [root / name for name in names]
    for path in paths:
        resolved = path.resolve()
        if root not in resolved.parents or path.is_symlink():
            raise ValueError('Workspace child escapes root or is a symlink: ' + str(path))
        if path.exists() and not path.is_dir():
            raise ValueError('Existing file occupies directory: ' + str(path))
    return root, paths


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--workspace', type=Path, default=Path.home() / 'locker-workspace')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if sys.platform != 'linux':
        raise SystemExit('Run inside the existing Ubuntu VM; this command does not create Windows directories.')
    if args.workspace.expanduser().is_symlink():
        raise SystemExit('Refusing a symlink workspace root')
    root, paths = layout_paths(args.workspace, Path(__file__).resolve().parents[1])
    if not args.dry_run:
        for path in paths:
            path.mkdir(parents=True, exist_ok=True)
    print(json.dumps({'workspace': str(root), 'directories': [str(p) for p in paths],
        'created_or_verified': not args.dry_run, 'sources_moved': False, 'build_or_tests_executed': False}, indent=2))


if __name__ == '__main__':
    main()
