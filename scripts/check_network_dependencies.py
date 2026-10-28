"""Verify S1-05 network source inputs; never downloads or modifies files."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    lock = json.loads((ROOT / 'deployment/host/dependencies.lock.json').read_text(encoding='utf-8'))
    manifest = json.loads((ROOT / 'deployment/host/network-sources.json').read_text(encoding='utf-8'))
    for name, key in [('civetweb', 'civetweb'), ('libsodium', 'libsodium'), ('openssl', 'tls_library')]:
        dependency = lock['dependencies'][key]
        if dependency.get('verified') is not True:
            raise RuntimeError('Unverified dependency: ' + key)
        if manifest['archives'][name]['archive_sha256'] != dependency['archive_sha256']:
            raise RuntimeError('Archive/lock mismatch: ' + key)
    for relative, digest in manifest['files'].items():
        path = (ROOT / relative).resolve()
        if not path.is_relative_to((ROOT / 'host/http/third_party').resolve()):
            raise RuntimeError('Manifest path outside vendor root')
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise RuntimeError('Vendor file mismatch: ' + relative)
    print(json.dumps({'status': 'PASS', 'network_source_files': len(manifest['files']),
                      'downloaded_at_configure': False}))


if __name__ == '__main__':
    main()
