"""S2-06 local pairing export for one cabinet.

The pairing file is a local configuration exchange format, not an API. It carries
public material only: the HTTPS base URL, the device id, the leaf certificate as
DER and the full SHA-256 of that DER. It never contains a password, a session
token, a private key, client authorisation or GPIO data.

    python deployment/target/export-pairing.py \
        --cert /data/locker/tls/server.crt --device-id 000000000000000000000000 \
        --host 192.0.2.20 --out pairing-192.0.2.20.json

The importer recomputes the fingerprint itself; the sha256 field only lets a human
compare, it is not proof of trust.
"""
import argparse
import base64
import hashlib
import json
import re
import ssl
import sys
from pathlib import Path

MAX_FILE = 8192
MAX_DER = 4096
FORBIDDEN = {'password', 'token', 'session_token', 'private_key', 'credential', 'gpio'}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cert', type=Path, required=True, help='leaf certificate (PEM or DER)')
    parser.add_argument('--device-id', required=True)
    parser.add_argument('--host', required=True, help='fixed host or IP, no scheme')
    parser.add_argument('--port', type=int, default=8443)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()

    if not re.fullmatch(r'[0-9a-f]{24}', args.device_id):
        parser.error('device id must be 24 lowercase hex characters')
    if not re.fullmatch(r'[A-Za-z0-9.-]+', args.host):
        parser.error('host must be a bare hostname or IPv4 literal')
    if not 1 <= args.port <= 65535:
        parser.error('port out of range')

    pem = args.cert.read_bytes()
    if pem.startswith(b'-----BEGIN'):
        der = ssl.PEM_cert_to_DER_cert(pem.decode('ascii'))
    else:
        der = pem
    if len(der) > MAX_DER:
        parser.error('certificate DER exceeds %d bytes' % MAX_DER)
    # Parse once so a malformed certificate cannot be exported as "public material".
    ssl.DER_cert_to_PEM_cert(der)

    document = {
        'format_version': 1,
        'https_base_url': 'https://%s:%d/api/v1' % (args.host, args.port),
        'device_id': args.device_id,
        'certificate_der_base64': base64.b64encode(der).decode('ascii'),
        'certificate_sha256': hashlib.sha256(der).hexdigest(),
    }
    leaked = FORBIDDEN.intersection(document)
    if leaked:
        sys.exit('refusing to export secret fields: %s' % ', '.join(sorted(leaked)))
    text = json.dumps(document, ensure_ascii=False, separators=(',', ':')) + '\n'
    if len(text.encode('utf-8')) > MAX_FILE:
        sys.exit('pairing file exceeds %d bytes' % MAX_FILE)
    args.out.write_text(text, encoding='utf-8')
    print(json.dumps({'out': str(args.out), 'bytes': len(text.encode('utf-8')),
                      'der_bytes': len(der), 'certificate_sha256': document['certificate_sha256'],
                      'https_base_url': document['https_base_url']}, ensure_ascii=False))


if __name__ == '__main__':
    main()
