import os, re, shutil, subprocess, sys
BASE=os.path.expanduser('~/s102_r9m26c')
BUILDS=os.path.expanduser('~/s102_r9m26cbuild')
SRC='/mnt/e/stm32项目'
OUT='/mnt/c/Users/Public/s102_r9_m26c.log'
EDITS=[('host/storage/src/repository.c',
 'if (is_partial != 0) {\n                    status = LOCKER_STATUS_CONFLICT;\n                    break;\n                }',
 'if (is_partial != 0) {\n                    continue;\n                }','if (is_partial != 0) {'),
 ('host/storage/src/repository.c','status = verify_official_objects(db);','status = LOCKER_STATUS_OK;','verify_official_objects(db);'),
 ('host/storage/src/repository.c','return verify_official_ddl(db);','return LOCKER_STATUS_OK;','return verify_official_ddl(db);')]
shutil.rmtree(BASE,ignore_errors=True); shutil.rmtree(BUILDS,ignore_errors=True)
os.makedirs(BASE); os.makedirs(BUILDS)
for item in ['CMakeLists.txt','contracts','host','tests']:
    s=os.path.join(SRC,item); d=os.path.join(BASE,item)
    if os.path.isdir(s): subprocess.run(['cp','-a',s,d],check=True)
    else: shutil.copy2(s,d)
log=[]
def w(s):
    log.append(s); print(s,flush=True)
w('# m26c_partial_plus_objects_plus_ddl = m26 + m28 + m33 (triple joint)')
for rel,old,new,needle in EDITS:
    p=os.path.join(BASE,rel)
    txt=open(p,encoding='utf-8').read()
    c=txt.count(old)
    w('edit %s count=%d' % (rel,c))
    if c!=1: w('PATCH_PATTERN_COUNT_ABORT'); sys.exit(1)
    open(p,'w',encoding='utf-8',newline='').write(txt.replace(old,new))
w('patch=OK')
b=os.path.join(BUILDS,'b')
r=subprocess.run(['cmake','-S',BASE,'-B',b,'-DCMAKE_BUILD_TYPE=Debug','-DLOCKER_BUILD_HOST=ON','-DLOCKER_BUILD_TESTS=ON'],capture_output=True,text=True)
w('configure=%d' % r.returncode)
r=subprocess.run(['cmake','--build',b,'-j4'],capture_output=True,text=True)
w('build=%d' % r.returncode)
if r.returncode!=0: w(r.stdout[-3000:]); w(r.stderr[-3000:]); sys.exit(1)
r=subprocess.run(['ctest','--test-dir',b,'--output-on-failure'],capture_output=True,text=True)
w('ctest=%d' % r.returncode)
out=r.stdout+r.stderr
for line in out.splitlines():
    if line.startswith('FAIL '): w(line)
m=re.search(r'\((\d+) of (\d+) checks failed\)',out)
if m: w('assertion_failures=%s denom=%s' % (m.group(1),m.group(2)))
open(OUT,'w',encoding='utf-8').write('\n'.join(log)+'\n')
w('DONE')
