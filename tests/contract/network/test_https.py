"""Real TLS requests against the Core + SQLite fixture; keys exist only at runtime."""
import http.client
import hashlib
import json
import os
import re
import socket
import ssl
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'scripts'))
from check_contracts import validate_value
API=json.loads((ROOT/'contracts/openapi.json').read_text())
SCHEMAS=API['components']['schemas']
ERRORS={e['code']:e for e in json.loads((ROOT/'contracts/contract-source.json').read_text())['error_codes']}

with tempfile.TemporaryDirectory(prefix='locker-https-') as directory:
    root = Path(directory)
    cert, key = root/'cert.pem', root/'key.pem'
    subprocess.run([sys.argv[2], 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-config', '/dev/null',
                    '-keyout', str(key), '-out', str(cert), '-days', '1',
                    '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    key.chmod(0o600)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    fast=hashlib.sha256()
    for value in ('11111111-1111-4111-8111-111111111111','USER_CREATE','00000000-0000-0000-0000-000000000000'):
        fast.update(value.encode()); fast.update(b'\0')
    credential={'username':'newuser','display_name':'中文😀','password':'new-user-password','role':'USER'}
    for name in sorted(credential):
        fast.update(name.encode()+b':'+json.dumps(credential[name],ensure_ascii=False,separators=(',',':')).encode()+b'\0')
    server = subprocess.Popen([sys.argv[1], str(cert), str(key), str(port), str(root/'service.sock'),fast.hexdigest()],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        assert server.stdout.readline().strip() == 'HTTPS_READY'
        untrusted = http.client.HTTPSConnection('localhost', port, timeout=4)
        try:
            untrusted.request('GET', '/api/v1/device')
            raise AssertionError('Untrusted certificate accepted')
        except ssl.SSLCertVerificationError:
            pass
        finally:
            untrusted.close()
        context = ssl.create_default_context(cafile=str(cert))
        def request(method, path, body=None, token=None, request_id=None, key=None):
            headers = {'Content-Type': 'application/json'}
            if request_id is not None:
                headers['X-Request-ID'] = request_id
            if key is not None:
                headers['Idempotency-Key'] = key
            if token:
                headers['Authorization'] = 'Bearer ' + token
            connection = http.client.HTTPSConnection('localhost', port, context=context, timeout=4)
            try:
                connection.request(method, '/api/v1' + path, body, headers)
                response = connection.getresponse()
                raw=response.read()
                if raw:
                    value=json.loads(raw)
                    if response.status>=400:
                        schema=SCHEMAS['Error']; expected=ERRORS[value['error']['code']]
                        assert expected['http']==response.status and expected['retryable']==value['error']['retryable'], value
                    else:
                        route=next(v[method.lower()] for p,v in API['paths'].items() if method.lower() in v and re.fullmatch(re.sub(r'\{[^}]+\}', '[^/]+', p),path.split('?')[0]))
                        schema=route['responses'][str(response.status)]['content']['application/json']['schema']
                    assert validate_value(schema,value,SCHEMAS), (method,path,response.status,value)
                return response.status, raw
            finally:
                connection.close()
        assert request('GET', '/device')[0] == 401
        assert request('POST', '/presence', '{}')[0] == 404
        assert request('POST', '/sessions', 'x'*4097)[0] == 400
        assert request('POST', '/sessions', '{"username":"tester","password":"wrong-password"}')[0] == 401
        time.sleep(1.1)
        code, body = request('POST', '/sessions', json.dumps({'username':'tester', 'password':'repository-auth-password'}))
        assert code == 200, (code, body)
        token = json.loads(body)['data']['token']
        def rpc(packet):
            with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as client:
                client.settimeout(3)
                client.connect(str(root/'service.sock'))
                try:
                    client.sendall(packet if isinstance(packet, bytes) else json.dumps(packet).encode())
                    return client.recv(65536)
                except (ConnectionResetError,BrokenPipeError):
                    return b''
        rpc_id = '55555555-5555-4555-8555-555555555555'
        envelope = {'v':1, 'request_id':rpc_id, 'method':'getCurrentSession',
                    'session_token':token, 'path_ids':{}, 'query':{}, 'idempotency_key':None, 'params':None}
        result = json.loads(rpc(envelope))
        assert result['status'] == 200 and result['body']['data']['username'] == 'tester', result
        assert result['request_id'] == result['body']['request_id'] == rpc_id
        assert json.loads(rpc(dict(envelope, session_token=None)))['status'] == 401
        assert json.loads(rpc(dict(envelope, source='LOCAL_UI')))['status'] == 400
        assert json.loads(rpc(dict(envelope, method='health.get')))['status'] == 404
        assert rpc(b'x'*8193) == b''
        assert rpc(b'{"request_id":"bad"}') == b''
        (root/'service.sock').chmod(0o666)
        assert rpc(envelope) == b''
        (root/'service.sock').chmod(0o600)
        with socket.socket() as probe:
            probe.bind(('127.0.0.1',0)); denied_port=probe.getsockname()[1]
        denied=subprocess.Popen([sys.argv[1],str(cert),str(key),str(denied_port),str(root/'denied.sock')],
                                stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,
                                env=dict(os.environ,LOCKER_TEST_IPC_DENY_PEER='1'))
        try:
            assert denied.stdout.readline().strip()=='HTTPS_READY'
            with socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET) as client:
                client.settimeout(3); client.connect(str(root/'denied.sock'))
                try:
                    client.sendall(json.dumps(envelope).encode()); rejected=client.recv(65536)
                except (ConnectionResetError,BrokenPipeError): rejected=b''
                assert rejected==b''
        finally:
            denied.stdin.write('\n'); denied.stdin.flush(); assert denied.wait(timeout=15)==0

        code, body = request('GET', '/sessions/current', token=token)
        assert code == 200 and json.loads(body)['data']['username'] == 'tester'
        assert b'password_hash' not in body and token.encode() not in body
        correlation = '22222222-2222-4222-8222-222222222222'
        code, body = request('GET', '/sessions/current', token=token, request_id=correlation)
        assert code == 200 and json.loads(body)['request_id'] == correlation
        assert request('GET', '/device', token=token, request_id='bad-id')[0] == 400
        for resource in ('tools', 'operations'):
            assert request('GET', '/' + resource + '/' + correlation)[0] == 401
            code, body = request('GET', '/' + resource + '/' + correlation, token=token, request_id=correlation)
            assert code == 404 and json.loads(body)['error']['code'] == 'NOT_FOUND', (resource, code, body)
            assert json.loads(body)['request_id'] == correlation
            for invalid in ('bad-id', correlation + '/extra', 'AAAAAAAA-2222-4222-8222-222222222222'):
                assert request('GET', '/' + resource + '/' + invalid, token=token)[0] == 400
        valid = {'tool_id': correlation, 'kind': 'BORROW', 'tool_revision': '1'}
        assert request('POST', '/operations', json.dumps(valid), key=correlation)[0] == 401
        for bad_key in (None, 'bad', '00000000-0000-0000-0000-000000000000'):
            assert request('POST', '/operations', json.dumps(valid), token=token, key=bad_key)[0] == 400
        for invalid_body in ('{}', '[]', '{"tool_id":', 'x'*4097,
                             json.dumps(dict(valid, actor_id=correlation)),
                             json.dumps(dict(valid, tool_revision=1)),
                             json.dumps(dict(valid, tool_revision='18446744073709551616')),
                             json.dumps(dict(valid, tool_revision='01')),
                             json.dumps(dict(valid, kind='OPEN')),
                             json.dumps(dict(valid, tool_id='bad'))):
            code, body = request('POST', '/operations', invalid_body, token=token, key=correlation)
            assert code == 400, (invalid_body, code, body)
        # Actual Core has no connected device in this fixture: valid intent fails closed.
        code, body = request('POST', '/operations', json.dumps(valid), token=token, key=correlation)
        assert code == 503 and json.loads(body)['error']['code'] == 'DEVICE_UNAVAILABLE', (code, body)
        for action in ('confirm', 'cancel'):
            code, body = request('POST', '/operations/' + correlation + '/' + action,
                                 '{"operation_revision":"1","reason":"cancel"}', token=token, key=correlation)
            assert code == 404 and json.loads(body)['error']['code'] == 'NOT_FOUND', (action, code, body)
            assert request('POST', '/operations/' + correlation + '/' + action + '/extra',
                           '{}', token=token, key=correlation)[0] == 404
        ws = context.wrap_socket(socket.create_connection(('localhost', port), timeout=4), server_hostname='localhost')
        ws.sendall(('GET /api/v1/ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                    'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n'
                    'Authorization: Bearer ' + token + '\r\n\r\n').encode())
        upgrade = b''
        while not upgrade.endswith(b'\r\n\r\n'):
            part = ws.recv(1)
            assert part, upgrade
            upgrade += part
        assert b'101 Switching' in upgrade, upgrade
        user_key = '33333333-3333-4333-8333-333333333333'
        user_body = json.dumps({'username':'newuser', 'display_name':'中文😀',
                                'password':'new-user-password', 'role':'USER'})
        code, body = request('POST', '/users', user_body, token=token, key=user_key)
        assert code == 201, (code, body)
        frame = ws.recv(2)
        assert len(frame) == 2 and frame[0] == 0x81, frame
        size = frame[1] & 127
        assert size < 126, size
        payload = b''
        while len(payload) < size:
            payload += ws.recv(size - len(payload))
        assert json.loads(payload)['type'] == 'resync_required', payload
        # Leave the authenticated WS unread while a fixed burst overruns its cursor.
        server.stdin.write('B'); server.stdin.flush()
        assert server.stdout.readline().strip() == 'BURST_DONE'
        assert request('GET','/sessions/current',token=token)[0] == 200
        ws.sendall(bytes([0x81,0xff])+(1000000000).to_bytes(8,'big')+b'abcd')
        # The frame policy rejects the header before allocating or waiting for its payload.
        try:
            while ws.recv(4096): pass
        except (ConnectionResetError,ssl.SSLError): pass
        assert request('GET','/sessions/current',token=token)[0]==200
        ws.close()
        user = json.loads(body)['data']
        assert user['username'] == 'newuser' and user['user_revision'] == '1'
        assert b'password' not in body
        code, replay = request('POST', '/users', user_body, token=token, key=user_key)
        assert code == 201 and replay == body, (code, replay)
        code, body = request('POST', '/users', json.dumps(dict(json.loads(user_body),display_name='Other User')), token=token, key=user_key)
        assert code == 409 and json.loads(body)['error']['code'] == 'IDEMPOTENCY_CONFLICT', (code, body)
        code, body = request('POST', '/users', user_body, token=token, key=correlation)
        assert code == 409, (code, body)
        code, body = request('GET', '/users', token=token)
        assert code == 200 and len(json.loads(body)['data']['items']) == 2, (code, body)
        assert b'password_hash' not in body
        time.sleep(1.1)
        code, body = request('POST', '/sessions', json.dumps({'username':'newuser','password':'new-user-password'}))
        assert code == 200, (code, body)
        user_token = json.loads(body)['data']['token']
        assert request('GET', '/users', token=user_token)[0] == 403
        assert request('POST', '/users', user_body, token=user_token, key=user_key)[0] == 403
        update_key = '44444444-4444-4444-8444-444444444444'
        update_body = json.dumps({'user_revision':'1', 'enabled':False})
        code, body = request('PATCH', '/users/' + user['id'], update_body, token=token, key=update_key)
        assert code == 200 and not json.loads(body)['data']['enabled'], (code, body)
        assert request('GET', '/sessions/current', token=user_token)[0] == 401
        code, body = request('PATCH','/users/'+user['id'],json.dumps({'user_revision':'2','enabled':True}),token=token,key=correlation)
        assert code==200, (code,body)
        assert request('GET','/sessions/current',token=user_token)[0]==401
        code,body=request('GET','/sessions/current',token=token)
        admin_id=json.loads(body)['data']['id']
        code,body=request('PATCH','/users/'+admin_id,json.dumps({'user_revision':'1','enabled':False}),token=token,key='66666666-6666-4666-8666-666666666666')
        assert code==409, (code,body)
        assert request('GET','/sessions/current',token=token)[0]==200

        assert request('PATCH', '/users/' + user['id'], update_body, token=token, key=user_key)[0] == 409
        assert request('GET', '/device?actor_id=admin' , token=token)[0] == 400
        assert request('DELETE', '/sessions/current', token=token)[0] == 204
        assert request('GET', '/sessions/current', token=token)[0] == 401
        assert request('DELETE', '/sessions/current', token=token)[0] == 204
        time.sleep(1.1)
    finally:
        if server.poll() is None:
            server.stdin.write('\n'); server.stdin.flush()
        try:
            assert server.wait(timeout=15) == 0
        except subprocess.TimeoutExpired:
            server.kill(); server.wait()
            raise
print('HTTPS trust, authentication, revocation, body and route boundaries passed')
