"""Formal S1-05 route/role acceptance driver; runs against a fixed compiled fixture."""
import http.client
import json
import socket
import ssl
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path

binary, openssl, source = sys.argv[1:4]
sys.path.insert(0, str(Path(source)/'scripts'))
from check_contracts import validate_value
schemas=json.loads((Path(source)/'contracts/openapi.json').read_text())['components']['schemas']
resource='77777777-7777-4777-8777-777777777777'
user_id='11111111-1111-4111-8111-111111111111'
intent={'tool_id':resource,'kind':'BORROW','tool_revision':'1'}
revision={'operation_revision':'1'}
create={'username':'matrix-user','display_name':'Matrix','password':'matrix-test-password','role':'USER'}
admin=[('GET','/users',None),('POST','/users',create),
       ('PATCH','/users/'+user_id,{'user_revision':'1','enabled':False}),
       ('PUT','/users/'+user_id+'/card',{'user_revision':'1','observation_id':resource}),
       ('DELETE','/users/'+user_id+'/card?user_revision=1',None),
       ('GET','/card-observations',None),('GET','/reviews',None),
       ('POST','/operations/'+resource+'/resolve',dict(revision,physical_revision='1',resolution='NO_CHANGE',reason='matrix verification'))]
ordinary=[('GET','/sessions/current',None),('DELETE','/sessions/current',None),('GET','/device',None),
          ('GET','/snapshot',None),('GET','/tools',None),('GET','/tools/'+resource,None),
          ('POST','/operations',intent),('GET','/operations/'+resource,None),
          ('POST','/operations/'+resource+'/confirm',revision),
          ('POST','/operations/'+resource+'/cancel',dict(revision,reason='matrix cancellation')),
          ('GET','/loans',None),('GET','/events',None)]
checks=0
with tempfile.TemporaryDirectory(prefix='locker-matrix-') as directory:
    root=Path(directory); cert=root/'cert.pem'; key=root/'key.pem'
    subprocess.run([openssl,'req','-x509','-newkey','rsa:2048','-nodes','-config','/dev/null',
                    '-keyout',str(key),'-out',str(cert),'-days','1','-subj','/CN=localhost',
                    '-addext','subjectAltName=DNS:localhost'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    key.chmod(0o600)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1',0)); port=probe.getsockname()[1]
    server=subprocess.Popen([binary,str(cert),str(key),str(port),str(root/'service.sock')],
                            stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
    try:
        assert server.stdout.readline().strip()=='FLOW_READY'
        context=ssl.create_default_context(cafile=str(cert))
        def request(method,path,token=None,body=None,ws=False):
            headers={'Content-Type':'application/json','Idempotency-Key':str(uuid.uuid4())}
            if token: headers['Authorization']='Bearer '+token
            if ws: headers.update({'Upgrade':'websocket','Connection':'Upgrade','Sec-WebSocket-Key':'dGhlIHNhbXBsZSBub25jZQ==','Sec-WebSocket-Version':'13'})
            c=http.client.HTTPSConnection('localhost',port,context=context,timeout=5)
            try:
                c.request(method,'/api/v1'+path,json.dumps(body) if body is not None else None,headers)
                r=c.getresponse(); data=r.read()
                if r.status>=400: assert validate_value(schemas['Error'],json.loads(data),schemas),(method,path,r.status,data)
                return r.status,json.loads(data) if data else None
            finally: c.close()
        code,session=request('POST','/sessions',body={'username':'tester','password':'network-flow-password'})
        assert code==200
        token=session['data']['token']
        for method,path,body in ordinary+admin:
            assert request(method,path,body=body)[0]==401,(method,path,'anonymous')
            checks+=1
            expected=204 if method=='DELETE' and path=='/sessions/current' else 401
            assert request(method,path,'f'*64,body)[0]==expected,(method,path,'unknown token')
            checks+=1
        for method,path,body in admin:
            assert request(method,path,token,body)[0]==403,(method,path,'user')
            checks+=1
        assert request('GET','/ws',ws=True)[0]==401
        assert request('GET','/ws',token='f'*64,ws=True)[0]==401
        checks+=2
    finally:
        if server.poll() is None:
            server.stdin.write('\n'); server.stdin.flush()
        try: assert server.wait(timeout=15)==0
        except subprocess.TimeoutExpired:
            server.kill(); server.wait(); raise
print(json.dumps({'status':'PASS','route_role_cases':checks,'compiled_fixture':binary}))
