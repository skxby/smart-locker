"""S2-06 link-loss check: freezing the MCU must not replay an action.

Physical unplugging needs an operator, so the MCU is halted over SWD instead: the
board stops answering exactly as if the wire were cut, while the host keeps
running. What is asserted:

  * the host stops reporting a fresh physical observation;
  * after the link returns, the device handshake repeats (HELLO grows) and no
    action request is replayed (EXECUTE stays at zero).

The counter block is the transport statistics struct of the s205 build.
"""
import argparse
import json
import os
import ssl
import sys
import time
import urllib.request
from datetime import datetime
from pathlib import Path

TOOLS = Path('C:/Users/%USER%/Documents/locker-hardware-tools/pyocd-0.46.0')
sys.path.insert(0, str(TOOLS))
os.environ.setdefault('PYOCD_USB_BACKEND', 'pyusb')

from pyocd.core.helpers import ConnectHelper                       # noqa: E402

ROOT = Path(__file__).resolve().parents[4]
PACK = Path('E:/keil5/ARM/PACK/Keil/STM32F4xx_DFP/2.13.0')
PROBE = '000000000000000000000000'
STATS = 0x20011460 + 0xB70 - 160            # transport.stats in the s205 image
OPTIONS = {'target_override': 'stm32f407vgtx', 'pack': str(PACK),
           'frequency': 950000, 'connect_mode': 'attach', 'reset_type': 'sysresetreq',
           'auto_unlock': False, 'resume_on_disconnect': True,
           'hide_programming_progress': True, 'pack.debug_sequences.enable': False}
# contracts/uart-layouts.json messages[].type: the stats array is indexed by type-1.
NAMES = ['hello', 'get_snapshot', 'fetch_events', 'ack_persisted', 'resume_ready',
         'disarm', 'execute_unlock', 'query_command', 'host_health', 'time_anchor',
         'release_context']


def counters(target):
    words = [target.read32(STATS + 4 * i) for i in range(40)]
    values = [words[2 * i] | (words[2 * i + 1] << 32) for i in range(20)]
    out = dict(zip(NAMES, values[:11]))
    out.update(dict(zip(['bad_session', 'bad_payload', 'bad_version', 'unsupported',
                         'tx_full', 'stale_replies', 'rx_lost', 'dma_duplicates',
                         'dma_invalid'], values[11:])))
    return out


def login(host, ca, user, password):
    context = ssl.create_default_context(cafile=str(ca))
    body = json.dumps({'username': user, 'password': password}).encode()
    request = urllib.request.Request('https://%s:8443/api/v1/sessions' % host, data=body,
                                     method='POST', headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(request, context=context, timeout=10) as response:
        return json.loads(response.read().decode())['data']['token']


def device_state(host, ca, token):
    """One session token is reused: the service limits logins per minute, and a
    diagnostic must not lock itself out of the cabinet it is measuring."""
    context = ssl.create_default_context(cafile=str(ca))
    request = urllib.request.Request('https://%s:8443/api/v1/device' % host,
                                     headers={'Authorization': 'Bearer ' + token})
    with urllib.request.urlopen(request, context=context, timeout=10) as response:
        payload = json.loads(response.read().decode())['data']
    physical = payload.get('physical') or {}
    return {'mode': payload.get('mode'), 'device_id': payload.get('device_id'),
            'health_reasons': payload.get('health_reasons'),
            'observed_age_ms': physical.get('observed_age_ms'),
            'continuity_ok': physical.get('continuity_ok')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--ca', required=True, type=Path)
    parser.add_argument('--user', default='admin')
    parser.add_argument('--password', required=True)
    parser.add_argument('--freeze-s', type=float, default=8.0)
    args = parser.parse_args()
    result = {'started': datetime.now().astimezone().isoformat(), 'host': args.host,
              'freeze_seconds': args.freeze_s}

    session = ConnectHelper.session_with_chosen_probe(blocking=False, unique_id=PROBE,
                                                      options=OPTIONS)
    if session is None:
        sys.exit('ST-LINK not available')
    with session:
        target = session.target
        target.halt()
        target.flush()
        before = counters(target)
        target.resume()
        time.sleep(1.0)
        token = login(args.host, args.ca, args.user, args.password)
        result['device_before'] = device_state(args.host, args.ca, token)

        target.halt()                                   # equivalent to cutting the wire
        target.flush()
        result['frozen'] = True
        deadline = time.monotonic() + args.freeze_s
        result['device_while_frozen'] = device_state(args.host, args.ca, token)
        while time.monotonic() < deadline:
            time.sleep(0.5)
        result['device_after_wait'] = device_state(args.host, args.ca, token)
        target.resume()
        result['frozen'] = False
        time.sleep(6.0)                                 # let the host re-handshake

        target.halt()
        target.flush()
        after = counters(target)
        target.resume()
        result['counters_before'] = before
        result['counters_after'] = after
        result['hello_delta'] = after['hello'] - before['hello']
        result['execute_before'] = before['execute_unlock']
        result['execute_after'] = after['execute_unlock']
        result['device_after_recovery'] = device_state(args.host, args.ca, token)

    result['checks'] = {
        'no_action_replayed': after['execute_unlock'] == before['execute_unlock'],
        'handshake_repeated': (after['hello'] - before['hello']) >= 1,
        'age_grew_while_frozen': (result['device_after_wait'].get('observed_age_ms') or 0) >
                                 (result['device_before'].get('observed_age_ms') or 0),
    }
    result['finished'] = datetime.now().astimezone().isoformat()
    out = ROOT / 'docs/evidence/stage-02/S2-06/link-loss.json'
    out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('hello_delta', 'execute_before', 'execute_after',
                                             'checks', 'device_while_frozen',
                                             'device_after_recovery')}, ensure_ascii=False, indent=2))
    return 0 if all(result['checks'].values()) else 1


if __name__ == '__main__':
    sys.exit(main())
