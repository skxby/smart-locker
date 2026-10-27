
import os,shutil,subprocess,json,hashlib,re,sys

SRC='/mnt/e/stm32项目'
BASE=os.path.expanduser('~/s102_r9mut')
BUILDS=os.path.expanduser('~/s102_r9build')
OUT='/mnt/c/Users/Public/s102_r9mut.log'

def sh(cmd,cwd=None):
    return subprocess.run(['bash','-lc',cmd],capture_output=True,text=True,cwd=cwd)

def sha(p):
    return hashlib.sha256(open(p,'rb').read()).hexdigest()[:12]

def line_of(path,needle):
    with open(path,'r',encoding='utf-8') as f:
        for i,l in enumerate(f,1):
            if needle in l: return i
    return -1

# point: (name, audit_ref, [(relpath, old, new, anchor_line_needle)], expected_note)
POINTS=[
 ('m1_revision','round-1 B1',[('host/storage/src/repository.c',
   'if (current != expected) {','if (0) {','if (current != expected) {')]),
 ('m2_dedup','round-1 B1',[('host/storage/src/repository.c',
   'if (status == LOCKER_STATUS_OK && !exists) {','if (status == LOCKER_STATUS_OK) {','if (status == LOCKER_STATUS_OK && !exists) {')]),
 ('m3_adjacent','round-1 B1',[('host/storage/src/repository.c',
   'return next->event_seq == previous->event_seq + 1u;','return next->event_seq >= previous->event_seq;','return next->event_seq == previous->event_seq + 1u;')]),
 ('m4_u64_fallback','round-1 B2',[('host/storage/src/u64.c',
   'if (left_length != right_length)','if (0)','if (left_length != right_length)')]),
 ('m5_schema_additional','round-1 B5',[('host/storage/src/schema.c',
   'if (cJSON_IsFalse(additional) != 0)','if (0)','if (cJSON_IsFalse(additional) != 0)')]),
 ('m6_sync','round-1 B3',[('host/storage/src/repository.c',
   'PRAGMA synchronous = FULL','PRAGMA synchronous = OFF','PRAGMA synchronous = FULL')]),
 ('m7_gap_latch','round-1 B1',[('host/storage/src/repository.c',
   'if (batch->gap_before_page) {','if (0) {','if (batch->gap_before_page) {')]),
 ('m8_trace_guard','round-1 B3',[('host/storage/src/repository.c',
   '!documents_is_table && user_objects != 0u','0','!documents_is_table && user_objects != 0u')]),
 ('m9_page_budget','round-1 B4',[('host/storage/src/repository.c',
   'const size_t reserved_next_key = LOCKER_STORAGE_KEY_MAX * 6u + 8u;','const size_t reserved_next_key = 8u;','const size_t reserved_next_key')]),
 ('m9b_doc_bytes','round-1 B4',[('host/storage/src/repository.c',
   'const size_t document_bytes = strlen(document_text);','const size_t document_bytes = (size_t)key_bytes + (size_t)value_bytes;','const size_t document_bytes')]),
 ('m10_nul_guard_raw','round-1 B5',[('host/storage/src/schema.c',
   "if (json[index] == '\\0')","if (0)","if (json[index] == '\\0')")]),
 ('m11_parse_nul','round-1 B5',[('host/storage/src/repository.c',
   'if (locker_storage_json_text_has_embedded_nul(json, length)) {','if (0) {','locker_storage_json_text_has_embedded_nul(json, length)')]),
 ('m12_boundary_structure','round-2 R2-B2',[('host/storage/src/repository.c',
   'if (!event_key_structure_ok(&batch->last_committed) ||\n        !event_key_structure_ok(&batch->first_available) ||\n        !event_key_structure_ok(&batch->next_after)) {','if (0) {','!event_key_structure_ok(&batch->last_committed) ||')]),
 ('m13_first_available_zero','round-2 R2-B2',[('host/storage/src/repository.c',
   'if (locker_storage_event_key_is_zero(&batch->first_available)) {','if (0) {','if (locker_storage_event_key_is_zero(&batch->first_available)) {')]),
 ('m14_gap_latch_early','round-2 R2-B1',[('host/storage/src/repository.c',
   'if (!batch->gap_before_page) {','if (1) {','if (!batch->gap_before_page) {')]),
 ('m16_sqlite_prefix','round-3 B-1',[('host/storage/src/repository.c',
   "SELECT count(*) FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'","SELECT count(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'","SELECT count(*) FROM sqlite_master")]),
 ('m17_verify_columns','round-3 B-2',[('host/storage/src/repository.c',
   'status = verify_table_columns(db, &k_tables[index]);','status = LOCKER_STATUS_OK;','status = verify_table_columns(db, &k_tables[index]);')]),
 ('m18_verify_key_index','round-3 B-2',[('host/storage/src/repository.c',
   'status = verify_key_index(db, &k_tables[index]);','status = LOCKER_STATUS_OK;','status = verify_key_index(db, &k_tables[index]);')]),
 ('m20b_any_matching_index','round-4 B-1',[('host/storage/src/repository.c',
   'if (!matches) {\n                    status = LOCKER_STATUS_CONFLICT;\n                    break;\n                }\n                ++matched_unique;',
   'if (matches) {\n                    ++matched_unique;\n                }','if (!matches) {')]),
 ('m21b_skip_column_collation','round-4 B-1',[('host/storage/src/repository.c',
   'collation == NULL || !ascii_text_equal(collation, "BINARY")','collation == NULL || 0','collation == NULL || !ascii_text_equal(collation, "BINARY")')]),
 ('m22_skip_extra_unique_index','round-4 B-1',[('host/storage/src/repository.c',
   'if (!matches) {\n                    status = LOCKER_STATUS_CONFLICT;\n                    break;\n                }\n                ++matched_unique;',
   'if (!matches) {\n                    continue;\n                }\n                ++matched_unique;','if (!matches) {')]),
 ('m25_regex_bounded_zero','round-4 N-3',[('host/storage/src/regex.c',
   'if (min_value == 0u) {\n        result = frag_optional(parser, atom);\n        produced = 1u;\n    }',
   'if (min_value == 0u) {\n        result = frag_optional(parser, atom);\n        produced = 0u;\n    }','produced = 1u;')]),
 ('m26_partial_unique','round-5 B-1',[('host/storage/src/repository.c',
   'if (is_partial != 0) {\n                    status = LOCKER_STATUS_CONFLICT;\n                    break;\n                }',
   'if (is_partial != 0) {\n                    continue;\n                }','if (is_partial != 0) {')]),
 ('m27_regex_min_seed','round-4 N-3',[('host/storage/src/regex.c',
   'if (min_value > 0u) {\n        produced = min_value;\n    }',
   'if (0) {\n        produced = min_value;\n    }','if (min_value > 0u) {')]),
 ('m28_official_objects','round-6 B-1',[('host/storage/src/repository.c',
   'status = verify_official_objects(db);','status = LOCKER_STATUS_OK;','verify_official_objects(db);')]),
 ('m29_table_info','round-6 B-2',[('host/storage/src/repository.c',
   'SELECT name, type, [notnull], pk, hidden FROM pragma_table_xinfo(?1) ORDER BY cid',
   'SELECT name, type, [notnull], pk, 0 FROM pragma_table_info(?1) ORDER BY cid','pragma_table_xinfo')]),
 ('m30_hidden_only','round-6 B-2',[('host/storage/src/repository.c',
   'if (hidden != 0) {\n                    status = LOCKER_STATUS_CONFLICT;\n                    break;\n                }',
   'if (0) {\n                    status = LOCKER_STATUS_CONFLICT;\n                    break;\n                }','if (hidden != 0) {')]),
 ('m31_put_changes','round-6 B-1',[('host/storage/src/repository.c',
   'if (sqlite3_changes(db) != 1) {\n            status = LOCKER_STATUS_IO_ERROR;\n        }',
   'if (0) {\n            status = LOCKER_STATUS_IO_ERROR;\n        }','if (sqlite3_changes(db) != 1) {')]),
 ('m32_delete_changes','round-6 B-1',[('host/storage/src/repository.c',
   'if (status == LOCKER_STATUS_OK && existed && sqlite3_changes(db) != 1) {',
   'if (status == LOCKER_STATUS_OK && existed && 0) {','existed && sqlite3_changes(db) != 1')]),
 ('m15b_view_double','round-2 R2-B3',[
   ('host/storage/src/repository.c',"db, \"SELECT 1 FROM sqlite_master WHERE name = ?1\"","db, \"SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1\"",'SELECT 1 FROM sqlite_master WHERE name = ?1'),
   ('host/storage/src/repository.c','if (status == LOCKER_STATUS_OK && documents_any_object && !documents_is_table) {','if (0) {','documents_any_object && !documents_is_table'),
   ('host/storage/src/repository.c','!documents_is_table && user_objects != 0u','0','!documents_is_table && user_objects != 0u'),
  ]),
  ('m26b_partial_plus_objects','round-5 B-1 (joint)',[('host/storage/src/repository.c',
    'if (is_partial != 0) {\n                    status = LOCKER_STATUS_CONFLICT;\n                    break;\n                }',
    'if (is_partial != 0) {\n                    continue;\n                }','if (is_partial != 0) {'),
   ('host/storage/src/repository.c','status = verify_official_objects(db);','status = LOCKER_STATUS_OK;','verify_official_objects(db);')]),
 ('m33_ddl_skipped','round-7 B-1',[('host/storage/src/repository.c',
   'return verify_official_ddl(db);','return LOCKER_STATUS_OK;','return verify_official_ddl(db);')]),
 ('m34_ddl_length_only','round-7 B-1',[('host/storage/src/repository.c',
   'memcmp(stored, expected, expected_length) != 0','0','memcmp(stored, expected, expected_length) != 0')]),
 ('m34b_ddl_text_not_compared','round-7 B-1',[('host/storage/src/repository.c',
   'if (stored == NULL || strlen((const char *)stored) != expected_length ||\n            memcmp(stored, expected, expected_length) != 0) {',
   'if (stored == NULL) {','memcmp(stored, expected, expected_length)')]),
]

# round-7 B-1 遮蔽分析：新增的 verify_official_ddl（DDL 文本比对）在结构检查之后兜底，
# 使 9 个只关掉单项检查的变异变成 NOT_CAUGHT。这里为每个被遮蔽点位补一个联合点位
# （该项检查关掉 + DDL 比对也关掉），用来证明该防线在被兜底掩盖时仍有独立观测能力。
_by_name = {p[0]: p for p in POINTS}
JOINTS = []
for _nm in ['m17_verify_columns', 'm18_verify_key_index', 'm20b_any_matching_index',
            'm21b_skip_column_collation', 'm22_skip_extra_unique_index', 'm26_partial_unique',
            'm29_table_info', 'm30_hidden_only']:
    _edits = list(_by_name[_nm][2]) + [('host/storage/src/repository.c',
        'return verify_official_ddl(db);', 'return LOCKER_STATUS_OK;',
        'return verify_official_ddl(db);')]
    JOINTS.append((_nm + '_plus_ddl_off', 'round-7 B-1 joint (with m33)', _edits))
POINTS = POINTS + JOINTS

log=[]
def w(s):
    log.append(s)
    print(s,flush=True)

w('# batch start')
w('# source tree %s'%SRC)
for p in ['CMakeLists.txt','host/storage/src/repository.c','host/storage/src/schema.c','host/storage/src/u64.c','host/storage/src/event_key.c','host/storage/src/regex.c','tests/unit/storage/test_repository.c','tests/unit/storage/test_schema.c','tests/unit/storage/test_u64.c','tests/unit/storage/test_event_key.c','tests/unit/storage/test_regex.c']:
    w('# SOURCE_FINGERPRINT %s  %s  %d B'%(sha(os.path.join(SRC,p)),p,os.path.getsize(os.path.join(SRC,p))))

def prepare(dst):
    if os.path.isdir(dst): shutil.rmtree(dst)
    os.makedirs(dst)
    for item in ['CMakeLists.txt','host','tests','contracts']:
        s=os.path.join(SRC,item); d=os.path.join(dst,item)
        if os.path.isdir(s):
            sh('cp -a "%s" "%s"'%(s,d))
        else:
            shutil.copy2(s,d)

summary=[]
for name,ref,edits in POINTS:
    dst=os.path.join(BASE,name)
    prepare(dst)
    ok=True; anchors=[]
    for rel,old,new,needle in edits:
        path=os.path.join(dst,rel)
        data=open(path,'r',encoding='utf-8').read()
        cnt=data.count(old)
        if cnt!=1:
            w('=== %s ==='%name); w('PATCH_PATTERN_COUNT %d for %s'%(cnt,rel)); ok=False; break
        anchors.append('%s:%d'%(os.path.basename(rel),line_of(path,needle)))
        tmp=path+'.tmp'
        open(tmp,'w',encoding='utf-8',newline='').write(data.replace(old,new,1))
        os.replace(tmp,path)
    if not ok:
        summary.append((name,'PATTERN_FAIL',0,0,0,0)); continue
    bdir=os.path.join(BUILDS,name)
    if os.path.isdir(bdir): shutil.rmtree(bdir)
    os.makedirs(bdir)
    c=sh('cmake -S "%s" -B "%s" -DCMAKE_BUILD_TYPE=Debug -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON >/dev/null 2>&1'%(dst,bdir))
    rc_conf=c.returncode
    b=sh('make -j4 >/dev/null 2>&1',cwd=bdir)
    rc_build=b.returncode
    t=sh('ctest --output-on-failure 2>&1',cwd=bdir)
    rc_ctest=t.returncode
    out=t.stdout
    fails=[l.strip() for l in out.splitlines() if l.strip().startswith('FAIL ')]
    denom=re.findall(r'\((\d+) of (\d+) checks failed\)',out)
    n=len(fails); d=denom[0][1] if denom else '0'
    w('=== %s ==='%name)
    w('anchor=%s'%', '.join(anchors))
    w('audit_ref=%s'%ref)
    w('patch=OK configure=%d build=%d ctest=%d'%(rc_conf,rc_build,rc_ctest))
    w('assertion_failures=%d  storage (%d of %s checks failed)'%(n,n,d))
    for l in fails: w('  '+l)
    summary.append((name,'OK' if rc_ctest!=0 else 'NOT_CAUGHT',rc_ctest,n,int(d) if d.isdigit() else 0,rc_build))
    shutil.rmtree(bdir,ignore_errors=True)

w('# batch end')
w('# summary')
for s in summary: w('# %-30s %-10s ctest=%s fails=%s denom=%s build=%s'%s)
open(OUT,'w',encoding='utf-8').write('\n'.join(log)+'\n')
print('WROTE',OUT)