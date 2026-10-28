"""Contract-validated TLS + peer-authenticated IPC against real Core/SQLite/device simulator."""
import http.client
import json
import re
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'scripts'))
from check_contracts import validate_value
API = json.loads((ROOT/'contracts/openapi.json').read_text())
SCHEMAS = API['components']['schemas']
INTERNAL = json.loads((ROOT/'contracts/internal-data.schema.json').read_text())['$defs']
SCHEMAS = dict(SCHEMAS, **INTERNAL)
ERRORS={e['code']:e for e in json.loads((ROOT/'contracts/contract-source.json').read_text())['error_codes']}

def uid():
    return str(uuid.uuid4())

with tempfile.TemporaryDirectory(prefix='locker-flow-') as directory:
    root = Path(directory)
    cert, key, sock = root/'cert.pem', root/'key.pem', root/'service.sock'
    subprocess.run([sys.argv[2], 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-config', '/dev/null',
                    '-keyout', str(key), '-out', str(cert), '-days', '1', '-subj', '/CN=localhost',
                    '-addext', 'subjectAltName=DNS:localhost'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    key.chmod(0o600)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0)); port = probe.getsockname()[1]
    server = subprocess.Popen([sys.argv[1], str(cert), str(key), str(port), str(sock)],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        assert server.stdout.readline().strip() == 'FLOW_READY'
        context = ssl.create_default_context(cafile=str(cert))
        def request(method, path, token=None, body=None, idem=None):
            headers = {'Content-Type':'application/json'}
            if token: headers['Authorization'] = 'Bearer ' + token
            if idem: headers['Idempotency-Key'] = idem
            c = http.client.HTTPSConnection('localhost', port, context=context, timeout=5)
            try:
                c.request(method, '/api/v1'+path, json.dumps(body) if body is not None else None, headers)
                r = c.getresponse(); code = r.status; raw = r.read()
                data = json.loads(raw) if raw else None
                if code >= 400:
                    expected=ERRORS[data['error']['code']]
                    assert expected['http']==code and expected['retryable']==data['error']['retryable'], data
                    assert validate_value(SCHEMAS['Error'], data, SCHEMAS), (code, data)
                elif code != 204:
                    route = None
                    for pattern, verbs in API['paths'].items():
                        regex = re.sub(r'\{[^}]+\}', '[^/]+', pattern)
                        if re.fullmatch(regex,path.split('?')[0]) and method.lower() in verbs:
                            route = verbs[method.lower()]; break
                    assert route and str(code) in route['responses'], (method,path,code,data)
                    schema = route['responses'][str(code)]['content']['application/json']['schema']
                    assert validate_value(schema,data,SCHEMAS), (method,path,code,data)
                return code, data
            finally:
                c.close()
        def rpc(method, token, params, idem, paths=None, query=None):
            packet = {'v':1,'request_id':uid(),'method':method,'session_token':token,
                      'params':params,'idempotency_key':idem,'path_ids':paths or {},'query':query or {}}
            with socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET) as c:
                c.settimeout(5); c.connect(str(sock)); c.sendall(json.dumps(packet).encode())
                response = json.loads(c.recv(65536))
            assert validate_value(SCHEMAS['InternalRpcResponse'],response,SCHEMAS), response
            assert response['request_id'] == packet['request_id']
            if response['body'] is not None: assert response['body']['request_id'] == packet['request_id'], response
            return response['status'], response['body']
        code, admin = request('POST','/sessions',body={'username':'root','password':'network-flow-password'})
        assert code == 200, (code,admin)
        admin = admin['data']['token']; time.sleep(1.1)
        code, user = request('POST','/sessions',body={'username':'tester','password':'network-flow-password'})
        assert code == 200, (code,user)
        user = user['data']['token']
        for path in ('/device','/snapshot','/tools','/loans','/events','/card-observations','/reviews'):
            code, body = request('GET',path,admin)
            assert code == 200, (path,code,body)
        assert request('GET','/reviews',user)[0] == 403
        assert request('GET','/card-observations',user)[0] == 403
        for path in ('/loans?limit=0','/loans?limit=101','/loans?limit=1&limit=2','/events?actor_id=x'):
            assert request('GET',path,user)[0] == 400, path
        code, tools = request('GET','/tools',user)
        tool = tools['data']['items'][0]
        code, other = request('POST','/users',admin,{'username':'other','display_name':'Other','password':'other-user-password','role':'USER'},uid())
        assert code == 201, (code,other)
        time.sleep(1.1)
        code, other = request('POST','/sessions',body={'username':'other','password':'other-user-password'})
        assert code == 200, (code,other)
        other = other['data']['token']
        owner_id=request('GET','/sessions/current',user)[1]['data']['id']
        admin_id=request('GET','/sessions/current',admin)[1]['data']['id']
        def observe_card():
            server.stdin.write('C'); server.stdin.flush()
            assert server.stdout.readline().strip() == 'CARD_DONE'
            deadline=time.monotonic()+4
            while time.monotonic()<deadline:
                code, observed=request('GET','/card-observations',admin)
                assert code==200, (code,observed)
                items=observed['data']['items']
                if items and not items[0]['consumed']: return items[0]
                time.sleep(0.05)
            raise AssertionError(observed)
        observed=observe_card()
        key_bind=uid(); binding={'user_revision':'1','observation_id':observed['id']}
        code,bound=request('PUT','/users/'+owner_id+'/card',admin,binding,key_bind)
        assert code==200 and bound['data']['masked_uid']=='****3344', (code,bound)
        assert request('PUT','/users/'+owner_id+'/card',admin,binding,key_bind)==(200,bound)
        assert request('PUT','/users/'+admin_id+'/card',user,binding,uid())[0]==403
        observed=observe_card()
        code,conflict=request('PUT','/users/'+admin_id+'/card',admin,{'user_revision':'1','observation_id':observed['id']},uid())
        assert code==409 and conflict['error']['code']=='CARD_ALREADY_BOUND', (code,conflict)
        code,unbound=request('DELETE','/users/'+owner_id+'/card?user_revision=2',admin,idem=uid())
        assert code==200 and not unbound['data']['bound'], (code,unbound)
        code,bound=request('PUT','/users/'+owner_id+'/card',admin,{'user_revision':'3','observation_id':observed['id']},uid())
        assert code==200 and bound['data']['user_revision']=='4', (code,bound)
        def wait_operation(operation, state):
            deadline = time.monotonic()+6
            while time.monotonic()<deadline:
                code, current = request('GET','/operations/'+operation,user)
                assert code == 200, (code,current)
                if current['data']['state'] == state: return current['data']
                time.sleep(0.05)
            raise AssertionError((state,current))
        for kind, scene, expected_pulses in [('BORROW','T',1),('RETURN','R',2)]:
            idem = uid(); intent = {'tool_id':tool['id'],'kind':kind,'tool_revision':tool['tool_revision']}
            code, created = request('POST','/operations',user,intent,idem)
            assert code == 201, (code,created)
            operation = created['data']['id']
            assert request('POST','/operations',user,intent,idem) == (201,created)
            assert request('POST','/operations',user,dict(intent,tool_revision='999'),idem)[0] == 409
            assert request('GET','/operations/'+operation,other)[0] == 404
            assert request('GET','/snapshot',other)[1]['data']['active_operations']==[]
            assert request('POST','/operations/'+operation+'/cancel',other,{'operation_revision':created['data']['operation_revision'],'reason':'not mine'},uid())[0] == 404
            assert request('POST','/operations/'+operation+'/confirm',user,{'operation_revision':created['data']['operation_revision']},uid())[0] == 409
            assert request('POST','/presence',user,{},uid())[0] == 404
            key_presence = uid(); presence = {'operation_id':operation,'operation_revision':created['data']['operation_revision']}
            if kind=='BORROW':
                code, accepted = rpc('presence.submit',user,presence,key_presence)
                assert code == 200, (code,accepted)
                assert rpc('presence.submit',user,presence,key_presence)[0] == 200
            else:
                server.stdin.write('C'); server.stdin.flush()
                assert server.stdout.readline().strip()=='CARD_DONE'
            wait_operation(operation,'ACCESS_ACTIVE')
            server.stdin.write('P'); server.stdin.flush()
            assert server.stdout.readline().strip() == 'PULSES:'+str(expected_pulses)
            server.stdin.write(scene); server.stdin.flush()
            assert server.stdout.readline().strip() == 'SCENE_DONE'
            current = wait_operation(operation,'WAIT_CONFIRM')
            assert request('POST','/operations/'+operation+'/confirm',other,{'operation_revision':current['operation_revision']},uid())[0] == 404
            key_confirm = uid()
            code, done = request('POST','/operations/'+operation+'/confirm',user,{'operation_revision':current['operation_revision']},key_confirm)
            assert code == 200 and done['data']['state'] == 'COMPLETED', (code,done)
            assert request('POST','/operations/'+operation+'/confirm',user,{'operation_revision':current['operation_revision']},key_confirm) == (200,done)
            time.sleep(0.3)
        code, loans = request('GET','/loans?limit=1',user)
        assert code == 200 and len(loans['data']['items']) == 1, (code,loans)
        assert loans['data']['items'][0]['closed_at'] is not None
        assert request('GET','/loans',other)[1]['data']['items'] == []
        code, events = request('GET','/events?limit=1',admin)
        assert code == 200 and len(events['data']['items']) == 1, (code,events)
        cursor = events['data']['next_cursor']; seen = {events['data']['items'][0]['id']}
        while cursor:
            code, events = request('GET','/events?limit=1&cursor='+cursor,admin)
            assert code == 200, (code,events)
            for event in events['data']['items']:
                assert event['id'] not in seen; seen.add(event['id'])
            cursor = events['data']['next_cursor']
            assert len(seen)<1000
        assert len(seen)>2
        code, events = rpc('listEvents',admin,None,None,query={'limit':1})
        assert code == 200, (code,events)
        for attempt in range(5):
            code,snapshot=request('GET','/snapshot',admin)
            physical=snapshot['data']['device']['physical']
            params={'journal_epoch':physical['journal_epoch'],'boot_id':physical['boot_id'],
                    'physical_revision':physical['physical_revision'],'snapshot_revision':snapshot['snapshot_revision'],
                    'reason':'post-cycle maintenance verification'}
            maintenance_key=uid()
            code,maintenance=rpc('maintenance.reconcile',admin,params,maintenance_key)
            if code==200: break
            assert code==409, (code,maintenance)
            time.sleep(0.15)
        assert code==200, (code,maintenance)
        assert rpc('maintenance.reconcile',admin,params,maintenance_key)[0]==200
        assert rpc('maintenance.reconcile',user,params,uid())[0]==403
        server.stdin.write('P'); server.stdin.flush()
        assert server.stdout.readline().strip()=='PULSES:2'
    finally:
        if server.poll() is None:
            server.stdin.write('\n'); server.stdin.flush()
        try:
            assert server.wait(timeout=15) == 0
        except subprocess.TimeoutExpired:
            server.kill(); server.wait(); raise
print('TLS operation/idempotency/owner matrix, IPC presence, single simulated pulses and history pagination passed')
