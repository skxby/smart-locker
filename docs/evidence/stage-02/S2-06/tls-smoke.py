"""S2-06 LAN TLS smoke test from the PC.

Two things are checked against the cabinet controller, not against a mock:
  1. the certificate is accepted only when the client trusts the deployed CA copy
     and the address matches a SAN entry;
  2. HTTPS reaches the real Core and reports the device it is actually driving.

Usage:
  python docs/evidence/stage-02/S2-06/tls-smoke.py --host 192.0.2.20 \
      --ca build/http-deploy/tls/server.crt --user admin --password ...
"""
import argparse
import json
import ssl
import sys
import urllib.error
import urllib.request
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def call(url, ca, body=None, token=None, trust=True):
    if trust:
        context = ssl.create_default_context(cafile=str(ca))
    else:
        context = ssl.create_default_context()
        context.check_hostname = True
        context.verify_mode = ssl.CERT_REQUIRED
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(url, data=data, method='POST' if data else 'GET')
    if data:
        request.add_header('Content-Type', 'application/json')
    if token:
        request.add_header('Authorization', 'Bearer ' + token)
    with urllib.request.urlopen(request, context=context, timeout=10) as response:
        return response.status, json.loads(response.read().decode('utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--ca', required=True, type=Path)
    parser.add_argument('--user', default='admin')
    parser.add_argument('--password', required=True)
    args = parser.parse_args()
    base = 'https://%s:8443/api/v1' % args.host
    result = {'started': datetime.now().astimezone().isoformat(), 'host': args.host,
              'ca': str(args.ca), 'checks': {}}

    # 1. Untrusted client must be refused. This is the negative half of the
    #    acceptance: a cabinet that answers without a trusted certificate is a
    #    vulnerability, not a convenience.
    try:
        call(base + '/device', args.ca, trust=False)
        result['checks']['untrusted_rejected'] = {'ok': False, 'detail': 'answer accepted'}
    except ssl.SSLCertVerificationError as exc:
        result['checks']['untrusted_rejected'] = {'ok': True, 'detail': str(exc)}
    except urllib.error.URLError as exc:
        reason = getattr(exc, 'reason', exc)
        ok = isinstance(reason, ssl.SSLCertVerificationError)
        result['checks']['untrusted_rejected'] = {'ok': ok, 'detail': str(reason)}

    # 2. Trusted client logs in and reads the device the service is driving.
    status, session = call(base + '/sessions', args.ca,
                           body={'username': args.user, 'password': args.password})
    token = session['data']['token']
    result['checks']['login'] = {'ok': status == 200, 'status': status,
                                 'token_length': len(token),
                                 'user': session['data']['user'].get('username')}
    status, device = call(base + '/device', args.ca, token=token)
    data = device.get('data', {})
    result['checks']['device'] = {'ok': status == 200, 'status': status,
                                  'revision': device.get('snapshot_revision'),
                                  'payload': data}
    result['finished'] = datetime.now().astimezone().isoformat()
    out = ROOT / 'docs/evidence/stage-02/S2-06/tls-smoke.json'
    out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if all(v['ok'] for v in result['checks'].values()) else 1


if __name__ == '__main__':
    sys.exit(main())
