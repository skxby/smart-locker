# S1-02 round-3 独立审计

日期：2026-10-24（Asia/Shanghai）。主副本：%REPO%；契约 0.2.1 / REVIEW_DRAFT。审计对象是本轮重算指纹的当前源码，不以开发 AI 的整改表作为结论。

## 0. 结论

**FAIL：2 项阻塞缺陷，3 项非阻塞问题；不满足 S1-02 技术交接/DONE 条件。**

- B-1：用户对象计数把 LIKE 的下划线当成字面前缀，漏掉 sqlitex_audit 等合法用户对象，有痕迹库仍可进入初始化。
- B-2：verify_schema_tables 只查三张表的名字，不验证列与唯一约束；不兼容 schema 可 open 假成功。
- N-1：m15 使用语法非法 SQL，普通新库 open 的失败不是“同名视图回归被杀死”的证据。
- N-2：属性名 NUL 负例仍会先被 required/未知字段校验拒绝，整改声明与实际夹具不符。
- N-3：交接正文仍有分页边界与初始化条件的误导性说明，部分证据数字与原日志不符。

另有独立验证缺口：本轮 Linux configure/build/CTest、ASan+UBSan、TSan、C 端探针和 C mutation 均 **NOT_RUN**。不得把历史日志或以下 Python SQL/字节模型记成本轮 C PASS。即使修复两项源码缺陷，也须取得与修复后指纹绑定的新 Linux 证据并复审。

本轮只新增本报告；不改实现、正式测试、契约、stage-plan、summary/handoff 或旧审计，不置 DONE、不开始 S1-03，不 commit/push。

## 1. 审计范围、实际环境、命令与结果

### 1.1 阅读与证据等级

已按入口顺序核对 AGENTS、README、阶段入口/机器索引、S1-02 卡及公共验收、阶段 0、traceability、模块所有权、内部接口和 UART；核对内部 schema 的 15 条 x-rules、18 个集合、107 个 defs 及生成头原生 DTO/六端口。读取 host/storage 的 DDL、全部自有源/头、根/局部 CMake、抽取器、全部五个 storage C 测试及 common 的 test_support/numeric；复核前两轮审计、S1-01 前置证据、summary §5.6/§5.7、handoff §12、全部 26 个 .log、源清单及依赖说明。

区分三类证据：①本轮实际运行的 Windows Python/SQLite/指纹检查；②当前源码与正式测试的静态核对；③开发方已保存的 Linux 历史日志。②③均不冒充①中的 C 运行结果。此次是只读审计，没有新项目或较大实现方案；另查 SQLite 官方表达式/CREATE TABLE 文档核对 SQL 语义，没有下载或替换依赖。

主机实际 Python：3.14.6，Anaconda，AMD64；Python SQLite：3.53.2，_sqlite3.pyd 位于 E:/anaconda/DLLs/。被审仓储锁定的 SQLite 是 3.54.1，两者不同；SQL 探针只佐证所用语句语义，没有链接本项目 vendored SQLite，也没有调用 locker_repo_*。

普通 exec_command 在启动时失败：Failed to create unified exec process / helper_unknown_error: setup refresh had errors，未产生 shell 进程或退出码。随后使用可用 Node fs/crypto 与 child_process.execFile 的参数数组完成读取和子进程调用；没有提权、安装工具、修改 SSH/WSL 配置或绕过拒绝访问。

### 1.2 本轮实际命令

以下工作目录均为 %REPO%；Python 均带 -B，脚本通过 -c 参数在内存执行，不落临时文件。

| 命令/检查 | 退出码 | 实际结果 |
|---|---:|---|
| git status --short | 128 | fatal: not a git repository (or any of the parent directories): .git；根目录也无 .git，未初始化仓库 |
| python -B -X utf8 scripts/check_contracts.py | 0 | PASS；0.2.1，24 routes / 82 ports / 28 private RPC；C 声明仅 parse PASS；hardware_profile=UNVERIFIED，business_or_hardware_tested=false |
| python -B -X utf8 scripts/check_stage_plan.py | 0 | PASS；45 卡，active_task=S1-02，implementation_cards_done=1，runtime_or_hardware_tested=false |
| python -B -X utf8 scripts/check_vision_contracts.py | 0 | PASS；camera_owned=false、rknn_available=false、hardware_tested=false |
| python -B -X utf8 scripts/check_feasibility.py | 0 | PASS；runtime_or_hardware_tested=false |
| python -B -X utf8 host/storage/tools/extract_schema.py --check | 0 | OK，27525 B；47 个冻结 defs / 18 集合与当前契约抽取一致 |
| python -B -X utf8 -c（P1–P5，见下） | 各 0 | 5 组独立探针实际完成；异常 SQL 被脚本捕获并打印，0 表示探针执行完成，不表示被审功能 PASS |
| wsl.exe -e bash -lc 'uname -a; command -v cmake gcc python3; printf "HOME=%s\n" "$HOME"' | 4294967295 | 启动失败；未取得 Linux 环境 |
| wsl.exe -e /bin/true | 4294967295 | UTF-16 原输出：拒绝访问。错误代码: Wsl/Service/CreateInstance/E_ACCESSDENIED |
| ssh -o BatchMode=yes -o ConnectTimeout=8 locker-vm 'uname -a' | 255 | stdout/stderr 均空，未取得 guest 执行权；不能由此判断 VM 关机或具体网络故障 |
| ssh -V | 255 | stdout/stderr 均空 |
| where.exe cmake / gcc / clang（分别执行） | 各 1 | 当前 PATH 未找到；没有安装另一套工具链 |
| Node fs + crypto 重算 | 不适用 | 源清单 32/32 大小/SHA 匹配，范围无漏项；详见 §6 |

**本轮 NOT_RUN（退出码不适用，未启动）**：Linux configure、build、全部 12 个 CTest 调度、C 示例编译、5 个 storage C 测试、6 个 common C 测试、ASan+UBSan、TSan、16 个 C 变异、C open/query/import 最小复现、DDL/COMMIT/I/O 故障注入。没有向 WSL/VM 同步快照或创建构建目录；VM 当前 WORKSPACE-INDEX、活跃源码指纹、原始编译选项/二进制也未访问。

Linux 可用后应先按 VM 工作流核对当前卡快照，不覆盖旧源码；在 locker-workspace/build/S1-02/<新批次> 配置 -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON，再 build -j2、ctest --output-on-failure。sanitizer 分别使用独立构建目录，保留实际编译/链接插桩参数及日志；不能仅凭目录名认定插桩。上述是待执行步骤，没有本轮成功输出。

### 1.3 五组独立探针：结果

P1/P2/P3 使用 Python SQLite 内存库，P4 是 NUL 截断与项目契约校验模型，P5 是紧凑 UTF-8 JSON 字节模型。它们不是开发方 16 项 mutation 的重复执行，也不是 C Repository 替代实现。

| 探针 | 本轮输出要点 | 能证明/不能证明 |
|---|---|---|
| P1：用户对象前缀 | 只有 sqlitex_audit VIEW 或 TABLE 时，原 count=0，字面前缀 GLOB count=1；执行相同 DDL 后新增 meta/migrations/documents，原对象及值 7 保留，quick_check=ok | 证明原计数 SQL 漏项、DDL 可走通；C open 返回值为静态推导，NOT_RUN |
| P2：不兼容三表 | documents(x TEXT) 也得到三表各 (1,)、version=1、migration=1、quick_check=ok；实际查询报 no such column: value_json。列齐全但无 PK 时读取可 prepare，UPSERT 报 ON CONFLICT clause does not match any PRIMARY KEY or UNIQUE constraint | 证明现有检查 SQL 不能识别不兼容结构，不能声称 C open 已实测 |
| P3：m15 EXPLAIN | 原 SQL 与 type='table' 版本均可编译；type=(char)116 报 near "116": syntax error；仅 documents 视图时 user_objects 仍为 1 | m15 破坏的是所有 open 的 SQL prepare；合法的 table-only 替换仍由另一道对象计数防线拒绝该视图 |
| P4：NUL 属性 | na\u0000me → C 字符串前缀 'na' → ToolRecord 校验 False；name\u0000junk → 前缀 'name' → True | 原正式属性夹具不是“截断后合法 name”；这是明确标注的截断模型，C mutation NOT_RUN |
| P5：query/recovery 预算 | 256 个“验”：62+38 条，query 页 63852/39146 B，recovery 63874/39168 B；中文+引号+反斜杠+换行：76+24 条，query 63652/20146 B，recovery 63674/20168 B | 四种页均契约合法、非空、收齐 100 条，长度+NUL≤65536；模型预留 1160 B，query/recovery 文档预算 64277/64255 B；不等于 C 分页实跑 |

### 1.4 本轮探针完整源码

逐段用 python -B -X utf8 -c 运行，均未写文件。P1/P2 的 DDL 从当前文件读取。

#### P1

```python
import sqlite3,pathlib,json
s=pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf-8')
for kind in ('view','table'):
 c=sqlite3.connect(':memory:')
 c.execute('CREATE VIEW sqlitex_audit AS SELECT 7 AS kept' if kind=='view' else 'CREATE TABLE sqlitex_audit(kept INTEGER)')
 if kind=='table': c.execute('INSERT INTO sqlitex_audit VALUES (7)'); c.commit()
 n=c.execute("SELECT count(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'").fetchone()[0]
 exact=c.execute("SELECT count(*) FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'").fetchone()[0]
 before=c.execute('SELECT type,name FROM sqlite_master').fetchall()
 c.executescript('BEGIN IMMEDIATE;\n'+s);c.commit()
 print(json.dumps(dict(probe='P1',kind=kind,count=n,literal_prefix_count=exact,before=before,after=c.execute('SELECT type,name FROM sqlite_master').fetchall(),kept=c.execute('SELECT * FROM sqlitex_audit').fetchall(),quick=c.execute('PRAGMA quick_check').fetchone())))
print('Python SQLite',sqlite3.sqlite_version,'SQL model only; C open NOT_RUN')
```

#### P2

```python
import sqlite3,pathlib,json
s=pathlib.Path('host/storage/schema-v1.sql').read_text(encoding='utf-8')
for decl in ('CREATE TABLE documents(x TEXT);','CREATE TABLE documents(collection TEXT NOT NULL,key TEXT NOT NULL,value_json TEXT NOT NULL);'):
 c=sqlite3.connect(':memory:');c.executescript(s);c.execute('DROP TABLE documents');c.execute(decl)
 names=[c.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",(t,)).fetchone() for t in ('meta','migrations','documents')]
 print('P2',decl,'tables=',names,'metadata=',c.execute('SELECT * FROM meta').fetchall(),'migration=',c.execute('SELECT version FROM migrations WHERE version=1').fetchall(),'quick=',c.execute('PRAGMA quick_check').fetchone())
 try: c.execute('SELECT value_json FROM documents WHERE collection=? AND key=?',('tools','x')); print('READ_PREPARE_OK')
 except sqlite3.Error as e: print('READ_PREPARE_ERROR',str(e))
 try: c.execute("EXPLAIN INSERT INTO documents(collection,key,value_json) VALUES('tools','x','{}') ON CONFLICT(collection,key) DO UPDATE SET value_json=excluded.value_json");print('WRITE_PREPARE_OK')
 except sqlite3.Error as e: print('WRITE_PREPARE_ERROR',str(e))
print('SQL model only; C repository NOT_RUN')
```

#### P3

```python
import sqlite3
c=sqlite3.connect(':memory:');c.execute("CREATE VIEW documents AS SELECT 'x' AS key")
for sql in ('SELECT 1 FROM sqlite_master WHERE name = ?1','SELECT 1 FROM sqlite_master WHERE type = (char)116 AND name = ?1',"SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1"):
 try:
  c.execute('EXPLAIN '+sql,('documents',)).fetchall()
  print('P3 SQL_OK',repr(sql),'rows=',c.execute(sql,('documents',)).fetchall())
 except sqlite3.Error as e: print('P3 SQL_ERROR',repr(sql),str(e))
print('USER_OBJECT_COUNT',c.execute("SELECT count(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'").fetchone()[0])
print('Correct table-only db_object_exists mutant STILL rejected by user_objects guard; C mutant NOT_RUN')
```

#### P4

```python
import json,pathlib,sys
sys.path.insert(0,'scripts');from check_contracts import validate_value
s=json.loads(pathlib.Path('contracts/internal-data.schema.json').read_text(encoding='utf-8'))
base=dict(id='22222222-2222-4333-8444-555555555555',name='power supply',slot_id=1,tool_revision='2',enabled=True)
for raw in (r'na\u0000me',r'name\u0000junk'):
 key=json.loads('"'+raw+'"'); clipped=key.split('\0')[0]; value=dict(base);del value['name'];value[clipped]='power supply'
 print('P4',raw,'C_STRING_PREFIX',repr(clipped),'MODEL_SCHEMA_VALID',validate_value({'$ref':'#/$defs/ToolRecord'},value,s['$defs']))
print('NUL truncation model only; C guard/mutant NOT_RUN')
```

#### P5

```python
import json,pathlib,sys
sys.path.insert(0,'scripts');from check_contracts import validate_value
s=json.loads(pathlib.Path('contracts/internal-data.schema.json').read_text(encoding='utf-8'))
def enc(x):return json.dumps(x,separators=(',',':'),ensure_ascii=False).encode('utf-8')
for reason in ('验'*256,('验"\\\n'*64)):
 docs=[]
 for i in range(100):
  k=f'{i:08x}-0000-4000-8000-{i:012x}'
  d=dict(collection='audits',key=k,value=dict(id=k,actor_id=None,operation_id=None,type='PAGE_BUDGET',reason=reason,occurred_at=None,time_quality='HOST_SYNCED',source_event=None))
  assert validate_value({'$ref':'#/$defs/RepoDocument'},d,s['$defs']);docs.append(d)
 for recovery in (False,True):
  env=dict(schema_version=1,snapshot_revision=str(2**64-1))
  if recovery:env['collection']='audits'
  env.update(documents=[],next_after_key=None)
  budget=65536-1-(len(enc(env))-2)-(192*6+8)
  pages=[];start=0
  while start<len(docs):
   chosen=[];n=0
   for d in docs[start:]:
    size=len(enc(d))+(1 if chosen else 0)
    if n+size>budget:break
    chosen.append(d);n+=size
   assert chosen;start+=len(chosen);page=dict(env);page['snapshot_revision']='100';page['documents']=chosen;page['next_after_key']=chosen[-1]['key'] if start<len(docs) else None
   assert len(enc(page))+1<=65536
   assert validate_value({'$ref':'#/$defs/'+('RepositoryRecoveryPage' if recovery else 'RepositoryQueryResult')},page,s['$defs'])
   pages.append((len(chosen),len(enc(page))))
  print('P5',json.dumps(dict(reason_bytes=len(reason.encode()),recovery=recovery,reserved_next_key=1160,budget=budget,pages=pages,total=start)))
print('Python JSON byte model only; C query/recovery NOT_RUN')
```

## 2. 阻塞发现

以下 C 返回值均来自逐分支源码推导，C 最小复现 **NOT_RUN**；SQL 探针的真实结果已单独列出。没有把未运行的预期写成观察值。

### B-1 / P2：对象计数漏掉合法用户对象，仍把有痕迹库当作空目标

**文件:行号**：host/storage/src/repository.c:279（函数）、:287（计数 SQL）、:1891（有痕迹拒绝）、:1894（进入初始化）、:1902（执行 DDL）。

**触发条件/最小复现**：

1. 在独立测试目标中只创建 CREATE TABLE sqlitex_audit(kept INTEGER)，插入 7；或只创建 CREATE VIEW sqlitex_audit AS SELECT 7 AS kept。
2. 用 schema_version=1、initialize_empty=true 调用 locker_repo_open；事先记录全部对象和数据。
3. 此时 documents 不存在，db_object_exists(documents)=false。原 name NOT LIKE 'sqlite_%' 把 sqlitex_audit 排除，user_objects=0，于是不会命中 :1891，进入首次 DDL。
4. 当前 DDL 可完整建成三表，元数据/三表名/quick_check 全部通过。源码推导 open 返回 OK 且新增三表；原对象未被删，但旧目标已被误当新库修改。

**预期 vs 实际**：规约与 :1867–1873 注释要求“没有任何用户对象”才初始化；预期 CONFLICT、out=NULL、不新增业务结构。实际 SQL 探针 count=0、最终新增三表，quick_check=ok；C open 的 OK 是上述无其他失败条件路径的静态结论。initialize_empty=false 则会走 NOT_READY，也没有正确识别有痕迹目标。

**依据与影响**：这是 B3/R2-B3 相邻的漏检路径，不是要求接纳任意外部数据库。该路径违背首次空目标与已有/迁移异常目标必须区分的约束，使损坏/残留目标初始化假成功。SQLite 的 LIKE 下划线匹配任意一个字符；内部保留的是 sqlite_ 字面前缀，并不包括 sqlitex。见 [SQLite 表达式](https://sqlite.org/lang_expr.html) 与 [CREATE TABLE](https://www.sqlite.org/lang_createtable.html)。

**建议修复与回归**：准确排除字面 sqlite_ 前缀（例如 name NOT GLOB 'sqlite_*'，或显式 ESCAPE 的 LIKE）；把 table/view 及含 index/trigger 的有痕迹夹具纳入回归，分别覆盖 initialize_empty=true/false，核对对象与数据未被补建/覆盖。不要仅复制有相同通配符错误的测试计数 SQL。无需改变公共 API 或业务契约。

### B-2 / P2：三表名齐全不代表 schema 兼容，open 可假成功

**文件:行号**：host/storage/src/repository.c:1778–1791（verify_schema_tables）、:1742–1774（metadata）、:1919–1925（旧库检查）、:1952–1953（返回 repo/OK）；实际读取 :1214，UPSERT :414–415。

**触发条件/最小复现**：

1. 在一次性夹具中执行当前 schema-v1.sql，保留 version=1、revision=0、migration version=1。
2. 替换 documents 为 CREATE TABLE documents(x TEXT)。另一个独立夹具保留 collection/key/value_json 三列及 NOT NULL，但不建 (collection,key) 主键/唯一约束。
3. 分别以 initialize_empty=false/true 打开。documents_is_table=true；verify_schema_tables 只逐项 SELECT 表名，metadata 仍合法，quick_check=ok。
4. 源码推导两者均可返回非空 repo 与 OK。前者首次正常 query 才遇到 no such column: value_json；后者正常 PUT 的 ON CONFLICT(collection,key) 在 prepare 时才报无匹配主键/唯一约束。P2 实测得到这些 SQL 错误。

**预期 vs 实际**：预期在 open 阶段明确拒绝不兼容 schema，out=NULL，保留目标。实际只验证对象名字与版本标量，在 schema 已无法支持仓储基本 SQL 时仍宣布可用。quick_check 证明 SQLite 文件内部一致性，不能证明应用列、约束或排序规则正确。

**依据与影响**：S1-02 卡 :53/56、内部 schema x-rules 03/13 要求 (collection,key) 唯一、schema 不兼容明确报错及错误路径证据。当前函数和报告声称的“必要表结构校验”没有验证结构。缺唯一约束会失去声明的文档唯一性/UPSERT 前提；缺列则直接导致查询/恢复不可用。没有声称已有正式库一定发生了这类损坏，也没有声称本轮 C 调用观察到了数据丢失。

**建议修复与回归**：在允许返回可用 repo 前，验证必要列、类型/NOT NULL、主键或等价唯一索引及 BINARY 键比较等本模块依赖的不变量；迁移版本/标记不能代替这些检查。对缺列、缺唯一约束及合法完整库建立有对照的正式用例，两种 initialize_empty 均拒绝不兼容结构、不自动重建。检查应同时用于初始化提交前和已有库路径。

## 3. 非阻塞问题

### N-1 / P2：m15 的失败是 SQL 语法错误，不能证明 R2-B3 的定向回归

**位置**：mutation.log:178–190、:193–194；summary.md:234–236；repository.c:256。P3 的 EXPLAIN 实跑：

~~~text
SELECT 1 FROM sqlite_master WHERE type = (char)116 AND name = ?1
near "116": syntax error
~~~

该文本是传给 SQLite 的 SQL，不是 C 中字符强制转换。其目的也与 'table' 文本不同。日志最先失败的是 test_repository.c:347 的普通新库打开（期望 OK，得到 IO_ERROR），后续大量用例同样无法开库。这不是 documents 视图的 CONFLICT 断言因恢复旧缺陷而变红。

即使换成语法正确的 type='table'，现有 db_user_object_count 仍会数出 documents 视图，:1891 仍拒绝；不能预期这个单点变异必然恢复 R2-B3。应设计能实际恢复旧错误行为的有效变异或明确测试冗余防线，并保留 configure/build/test 各自退出码、目标场景失败行与完整指纹。

预期是“合法、可运行、仅改变目标语义的变异由目标断言捕获”；实际是公共 SQL prepare 先失败。16 条记录确实存在、14 条写失败/2 条写通过，但至少 m15 不能计为有意义的目标覆盖，不能沿用“14 个有效变异杀死、三个阻塞各有专门变异”的概括。本轮没有重跑其余 C 变异，不据此反推其他 13 条已独立证明，也不推断证据伪造。

### N-2 / P2：NUL 属性名负例仍为空洞，串行变异失败有连带效应

**位置**：test_repository.c:1876–1887 的 escaped-NUL 属性名、:1893–1899 的 raw-NUL 属性名；summary.md:284；schema.c:378–389；mutation.log:140–151。

两项夹具实际写的是 na + NUL + me。cJSON/C 字符串截断后是 **na**，不是注释/整改表声称的 **name**。即使删掉 NUL 守卫，ToolRecord 所需的 name 仍缺失，na 也不是允许字段，所以 INVALID 不能单独证明属性名 NUL 防护。P4 以当前项目 validate_value 对截断模型核对：na→False，name→True。应使用 name + NUL + junk 这类截断后恰好合法的属性，配完整合法变更和去 NUL 正对照。

key 开头的 NUL 负例 :1856–1863 截断后为空 key，也会被 minLength 拒绝；它有输入拒绝价值，但不能宣称该例独立杀死 NUL 守卫。其他中间 key/U64/raw reason 负例已比上一轮完整，当前两个正式入口的原始 0x00/escaped NUL 守卫也真实存在，**本项没有认定 B5 生产防护再次失效**。

m11 日志中第一项 :1844 在去守卫后变更成功，revision 从 1 到 2；随后仍用 expected=1 的 :1854/:1874 等会返回 CONFLICT。它们的“期望 INVALID 实得 CONFLICT”是先前误提交的连带影响，不能分别当成每种 NUL 都已命中守卫的证据。应给各类别独立库/恢复基线，明确逐例前后 revision/文档和目标断言；保留总非空洞性结论与类别覆盖结论的区别。

### N-3 / P3：当前说明仍有边界与证据数字错误

**位置/预期 vs 实际/建议**：

- handoff.md:159–163 把 first_available 固定写成本页第一条、last_committed 固定写成本页最后一条。UART-v1.md:368–370 中前者是日志最早可用位置，后者是响应的已提交尾，可早于/晚于当前页边界；只有 next_after 在非空页必须等于本页末条。示例应保留 UART 实际响应的三字段，而不是从本页重算，否则会抹掉真实保留范围/已提交尾。当前实现 :1547/:1559 允许严格包含本页的合法范围，正式成功助手全用边界相等，建议补一个 first_available < first_key < last_key < last_committed 的成功对照及 has_more=true 用例。
- handoff.md:284 仍写“没有任何用户表”即可首次初始化；summary.md:266 的 B3 行仍只写 db_user_table_count，且只描述初始化中的 metadata 校验。它们没有同步 §5.7/实际代码的“任意用户对象”和提交前 verify_schema_tables。应直接改当前正文，避免后续 Agent 依旧规则恢复漏洞。
- README.md:21 写 active_task=null，但机器索引实际为 S1-02；用户要求机器索引为准，本轮没有改状态。summary.md:166 写 WSL 普通 CTest 0.21s，ctest.log 实为 0.28s。应将当前入口和引用数字与真源/原日志对齐，不能改原日志凑说明。

这些文档项不单独判定生产运行失败；但收尾仍须同步。boot 严格增加且 seq=1 的正文已基本统一，不再沿用“必须 boot+1”的旧描述；不能仅由跳号推断中间启动必定没有事件，应依赖设备日志/缺口语义。

## 4. 历史问题逐条闭环情况

本表“静态闭环”只指指定反例已被当前代码及相关正式回归封住；**不等于本轮 C 执行 PASS**。所有 C 运行栏均 NOT_RUN。

| 历史 ID | 当前代码/测试/说明 | round-3 判断 |
|---|---|---|
| B1 | repository.c:1393 同 boot 严格 seq+1，防 UINT64_MAX；:1522 页内相邻；:1678/:1683 阻断缺口/回填倒退，:1693 锁存新 gap；test_repository.c:1157/1300/1445 相关回归 | 原页内缺号、有新事件 gap、历史回填反例静态闭环；全重复 gap 另见 R2-B1 |
| B2 | :1533 非空页零尾拒绝；:1544 同日志；:1547 末条≤已提交尾；:1556/:1559 首边界；:1563 next_after==末条 | 原全零尾/异设备或 epoch 绕过静态闭环；半零另见 R2-B2 |
| B3 | 真正空目标 DDL 在 :1900 BEGIN/:1913 COMMIT 内；异常分支 :1915 ROLLBACK；部分结构 :1891 拒绝；已初始化三表检查 | 原只有 meta 的反例已封；仍有 B-1 漏对象与 B-2 不兼容结构假成功，因此初始化/兼容性整体未闭环；DDL 中途失败注入仍无正式证据 |
| B4 | :950 真实信封预算，:952 1160 B 续页余量，:1076 完整文档序列化字节，装不下缩页；query/recovery 共用；test_pagination_budget | 核心原反例静态及 P5 字节模型闭环；m9 漏检如实保留；C 分页 NOT_RUN |
| B5 | repository.c:479 与 schema.c:35/42/55 的解析前防护，处理原始 NUL/奇偶反斜杠 Unicode NUL | 防护静态闭环；负例部分仍有 N-2 证据空洞，不能声称属性名类别已独立验证 |
| N1 | handoff.md:166 传 NULL，:191 区分 OK/连续性/ACK 水位不倒退不越界 | 无投影和 ACK 条件主要修正；示例边界说明仍见 N-3；例子未编译运行 |
| N2 | summary/handoff 限定正常 close/reopen，异常崩溃和介质掉电未宣称已验证 | 文档闭环；test_persistence_across_reopen 是正常重开，不外推崩溃恢复 |
| N3 | test_failed_transaction_is_atomic :583 用 trigger 使第二次 outbox 写失败，:609/:616 核对空集/revision，移除 trigger 后 :633 同输入成功 | SQL 半写回滚的测试设计有效；本轮 C NOT_RUN；FULL 同连接运行断言/m6 漏检仍未补，不据此认定生产设置 OFF |
| N4 | deployment 两文件现记录 SQLite/cJSON vendor 路径、消费者、逐文件 SHA 与许可；模块 README 已存在 | 旧“未 vendor/cJSON 仅 S1-05”说明已修；6 vendor 文件匹配 lock；原归档字节同源本轮未重新下载验证 |
| N5 | u64.c/schema.c 复用 common numeric；抽取器在树内，CTest 登记 --check；本轮直接 --check 通过 | 当前冻结副本闭环；CMake 缺 Python 时只打印 SKIPPED、不注册该项，不能称任何环境都固定 12 项 |
| R2-B1 | :1630 全重复分支中 gap=false 回滚；gap=true+stored=true 更新 continuity=false 并 COMMIT，through 用 stored_through，不应用 projection；无 stored 时 gap=true 返回 CONFLICT；test_duplicate_page_latches_gap :1402 | 原“new_count=0 丢 gap”反例静态闭环，:1429/:1438 核对锁存；m14 日志指向它，但本轮 C/mutation NOT_RUN。gap=false 无 stored 的历史异常分支仍按原行为，不能把 gap=true 的拒绝概括到全部重复页 |
| R2-B2 | :1380 两分量同时零/同时非零检查；事件 :1503/:1506 拒零/半零；边界 :1537 检查结构，first_available :1550 另拒 (0,0)；:1563 next_after 必须等末条 | 指定半零/非空页零首边界静态闭环；正式边界用例 :1243–1275 有同日志 (0,0) 与全结构置零；:1294 合法恢复对照；每条事件半零仍缺专用正式负例 |
| R2-B3 | :256 任意类型同名对象查找，:287 用户对象计数，:1886 非表同名拒绝；初始化 :1907 在 COMMIT 前查三表；test_documents_view_rejected :2003 两种开库均要求 CONFLICT | 原 documents 视图反例静态闭环；不扩大为所有 schema 痕迹闭环，B-1/B-2 仍在；m15 证据无效见 N-1 |
| R2-N1 | 健康连续性起点的 boot 跳号测试 :1366 已补，:1385 true→:1389 boot1→5→:1394 true；部分 NUL 输入已强化 | 部分闭环；NUL 属性空洞仍在；初始化 DDL 中途失败回滚仍未注入 |
| R2-N2 | handoff :182/:187、summary :264 使用 boot 严格增大、seq=1；handoff :191 写 ACK 条件 | 旧 boot+1 矛盾/OK 即 ACK 主要闭环；没有证据支持仓储自行证明“跳过的 boot 没有事件”，该假设须契约/设备层澄清 |
| R2-N3 | mutation.log:15–22 七个短指纹与本轮完整 SHA 前缀相同；m4 仍替换 u64.c:51，但新增非规范输入使该分支实际可达；m9b 分母改为 588，与日志一致 | 快照前缀/分母旧问题已改善；m4 不是“锚点代码换过”，是新增输入使原锚点可达。16 项总体有效性仍有 N-1/N-2，不能全视为独立复验 |
| R2-N4 | 预留明确 192*6+8=1160 B；m8 名 trace_guard，明确不是 DDL 事务测试；m6/m9 明记未捕获 | 数值与命名勘误闭环；m6 运行断言与 DDL 故障注入仍是已知缺口，不把缺口登记当作验证通过 |

## 5. S1-02 按卡验收与 DONE 判断

| 卡要求/边界 | 实现与证据 | 本轮评价 |
|---|---|---|
| 唯一 DDL、受控集合、六端口/原生 DTO | schema-v1.sql 为执行真源；18 集合，47 defs 冻结闭包，六签名对应生成头 :211/:213/:215/:273/:275/:277，结果/变更缓冲 65536 B | 静态成立，未见空成功端口或另造对外 API；直接抽取检查 PASS，不代表 C 构建 |
| 操作+幂等+outbox 原子提交 | commit_change :1350 parse→:1354 BEGIN IMMEDIATE→apply checks/revision/mutations→COMMIT；失败 rollback；正式 trigger 回归带正对照 | 测试设计与静态路径认可，C/真实 I/O 故障本轮 NOT_RUN |
| 两个竞争申请只能一个成功 | 正式并发测试用两个 fork 子进程分别开库，事务中检查 revision/ABSENT；没有 HTTP/UI 绕过 | 源码/历史日志可查，本轮并发运行 NOT_RUN |
| 重开保留有效借用、未决操作和幂等 | 正常 close/reopen 用例存/读相关集合 | 范围限定为正常重开；本轮 NOT_RUN，SIGKILL/掉电未证明 |
| 新事件+Core 投影+游标同事务，重复不重投影 | import :1580 BEGIN，插入新事实，:1699 可选投影，:1725 游标，同 COMMIT；全重复不应用投影，gap 单独锁存 | R2-B1 指定反例静态闭环；本轮 C import NOT_RUN |
| 游标不跨缺口、不倒退 | 相邻/同日志/范围/结构防线已补；gap false 不消除 stored false；boot 增大且 seq=1 | B1/B2/R2-B1/R2-B2 指定反例静态已封；保持依赖可信 gap/epoch 语义，不从跳号推断无缺失；运行 NOT_RUN |
| u64 数值顺序/10 在 2 后 | common 规范十进制溢出解析；event_key 原生 uint64 比较及 20 位补零 SQL key；新增非规范 fallback 用例 | 静态认可；C tests/m4 本轮 NOT_RUN |
| query/recovery 同时按行数及字节限页 | 完整 document JSON 字节、信封、续页键、NUL 都计入；P5 四组模型均有效并收齐 | 字节模型通过；C 实跑 NOT_RUN；1160 B 是保守余量，不认定去余量必然导致现有合法页越界 |
| WAL、synchronous=FULL | open :1860/:1864 明确设置；普通文件 WAL 正式回归存在 | 静态配置存在；WAL 返回模式未逐字检查、FULL 无同连接断言、m6 漏检；不推定 RK 介质掉电安全 |
| 首次初始化/损坏/schema 不兼容分离，不自动清库 | 显式 DDL 事务与已知损坏拒绝存在，但对象漏计及结构只查名字 | **FAIL：B-1/B-2**；没有自动删库代码，不能因此忽略假成功 |
| Core 单写/生命周期/错误资源 | repo 分配/关闭成对，失败 out=NULL；busy_timeout=5000ms；绑定参数，语句 finalize；不建线程/回调/串口/GPIO | 静态符合边界；只允许 Core 所有者调用，THREADSAFE=1 不是 cJSON 多线程许可；close 不能并发；OOM/磁盘满注入 NOT_RUN |
| DDL映射、错误路径、交接文档及当前证据 | summary/handoff/manifest 与日志齐备，部分结论有 N-1–N-3 | 证据质量及当前正文仍须修正；本轮不能 DONE |

机器状态当前为 S1-01=DONE、S1-02=IN_PROGRESS、active_task=S1-02、recommended_next=S1-02、S1-02 report=null、阶段1 gate_result=NOT_RUN。与本轮 FAIL/未交接一致，审计没有修改状态。

不覆盖：ARM/32 位构建、CMake 3.16 下限、RK eMMC/SD 断电/fsync 时延、MCU/littlefs、真实 UART、硬件锁脉冲截止/GPIO/电气、相机/RKNN/NPU、Android/LVGL、真实 HTTPS/IPC 和完整借还业务。它们均不能由本轮静态检查、SQL 模型或 x86 历史日志升级为已完成。

## 6. 输入身份、指纹与历史日志核验

### 6.1 当前 32 项源清单

本轮逐文件读取原始字节，SHA-256/大小 **32/32 全匹配**；根 CMakeLists.txt + host/storage/ + tests/unit/storage/ 范围内无未列文件。26 个自有文件与 6 个第三方文件均有清单；6 个 vendor 文件也与 dependencies.lock.json 的逐文件摘要一致。原依赖归档未重新取得，本轮不独立声称“与上游原归档逐字节一致”。

| 路径 | 字节数 | SHA-256（本轮重算） |
|---|---:|---|
| CMakeLists.txt | 3943 | f9e1964ee54465f5e29299721a3f342f929cc81e3ff6a6da8d00b77fc34258b4 |
| host/storage/CMakeLists.txt | 3782 | 3acc9e405c66569b988db32b7cb0e135b8a3a4c52fdd53f3e606e01fee49297a |
| host/storage/README.md | 7289 | 0fc68d2ea2825ac5b6fa05e96d3d168e50fe8f603ca11da6dc8c3e5bb0f779e3 |
| host/storage/cmake/embed_file.cmake | 1891 | c64242387b74017b03c9ce9559e5bc245eda0f83d5636ea989a3740ded028c7e |
| host/storage/include/locker/storage/embedded.h | 898 | f8087fd75b43b6c35d17fd400dacf95f6f4177b937e81278783a4d093441ab7a |
| host/storage/include/locker/storage/event_key.h | 1660 | 4d46736869d62e688540dc2c0a10f3c0c71fe6302898266b1d836f143c705d73 |
| host/storage/include/locker/storage/regex.h | 1130 | 39fdb69f6fb58397e82810c3778d497b117944449527fc5b4d040129b1c03ade |
| host/storage/include/locker/storage/schema.h | 2197 | 547f9b203eb922cdb6222c0ec419bfe77b0e6899dfa010b671c90704d6bd987f |
| host/storage/include/locker/storage/u64.h | 835 | 08cdc34af372ce6cb3d0b316b6270a827aa3cdf5f007b8f9f61bd39445959f27 |
| host/storage/schema-v1.sql | 2350 | 0eaa403ba7437c855d35a556443564ee6f1e9fd1cc2d5c967aa71c4f17ed8f70 |
| host/storage/src/event_key.c | 7165 | 2229c561f45518300ce74eff4fdc5d582f8b7c6a5a30f3c07aa1adb532369edb |
| host/storage/src/regex.c | 24788 | 20883688a8bc8a1710a2d38eb3abb3e769eb3106b1c107739251386260072c01 |
| host/storage/src/repository.c | 73995 | fd78d969cb4b143c9bd3e3ca1d7438a8262bed4c5ff0e9c8e9d397a812ea72fc |
| host/storage/src/repository_internal.h | 3344 | f68136968db1af077771b567b93dd974865294bb29c313f1928eec1748c6b64a |
| host/storage/src/schema.c | 26069 | 19bd5b4f9e2497258adc5254c81eaba6deb583194468fb2e0c32f886a93f1723 |
| host/storage/src/schema_embedded.c | 809 | fc0c2a336230556b2016a8f2b4e3987b18f71db717cb3f1e96d12d1c88652f46 |
| host/storage/src/schema_internal.h | 1022 | a5d1059e54b112fb232a1da7917d02b4739f5b2346ac18493dce2ac0e8e9f75a |
| host/storage/src/schema_records.json | 27525 | 781d7b139af23b5783040281e3dacc164a0d51076a11376e6bef1411941888ef |
| host/storage/src/u64.c | 2180 | 875036a0abbf6d49b15ec33128adbe24cae34df74133985f4abbbf3e04b26696 |
| host/storage/tools/extract_schema.py | 5355 | ab5990b55b661b8c7596002783a0f845266f0b5b7d91943d95578f1d117cb218 |
| tests/unit/storage/CMakeLists.txt | 2354 | bc3ccbbc732b04621e2f7a39f7434fc666a12876b0c24e31f86f1bd0e637963b |
| tests/unit/storage/test_event_key.c | 7913 | 159254c9091deb3b8d3c1e888ae8819caf2dcbd50882096cac9ef757feb2ca45 |
| tests/unit/storage/test_regex.c | 3265 | f7722cfbd371d0194ef73819c0547bd1a01b005c990a2e049f516821cb188513 |
| tests/unit/storage/test_repository.c | 98001 | f12b28f49dee177c1e1b9c379dcb82c1a6e6c588a3876dd8732d9df7d3802113 |
| tests/unit/storage/test_schema.c | 31269 | 4952adc2876f8a1c37d83ad42968ede1cd40d66a6a582862cd0d75941a20e2ca |
| tests/unit/storage/test_u64.c | 6040 | 6919ee37502373a4f0c19ef7ca6134820de333e150df37d353c02bb6ac9652e7 |
| host/storage/third_party/cjson/LICENSE | 1084 | a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c |
| host/storage/third_party/cjson/cJSON.c | 80399 | 298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d |
| host/storage/third_party/cjson/cJSON.h | 16394 | 25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee |
| host/storage/third_party/sqlite/sqlite3.c | 9582521 | 0c657666d819f261cc4007526ad57e1669de9949df7a8ea847aa284a2bf0ee52 |
| host/storage/third_party/sqlite/sqlite3.h | 695729 | d6c84557261893439b341d2e6c35187ae3ea24fbfd843ad94616f72d89242b8c |
| host/storage/third_party/sqlite/sqlite3ext.h | 39547 | 48963abfefb41ac329ef9d706acb5eadba952a99a748285c9ded6b5e0942721b |

### 6.2 契约、状态与审计输入

| 路径 | 字节数 | SHA-256（本轮重算） |
|---|---:|---|
| contracts/contract-source.json | 66553 | 14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f |
| contracts/internal-data.schema.json | 129569 | e7b8695c49115e2994cf126dc5ab629e5c82460e70d4c292b19576f55c60e386 |
| contracts/include/locker_contracts.h | 39925 | 34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a |
| docs/stages/stage-01-task-cards.md | 16460 | 7f7561e83f9f614fd4ed226cefe3ce78ba367859a55ee92cd82212b25273fc93 |
| config/development/stage-plan.json | 24749 | 1ed89d9f5695c40f965f43d331907295eccca21eaf52d67bee92e5dc4fdf1b47 |
| docs/evidence/stage-01/S1-02/source-manifest.txt | 3661 | 80face09d61ad02a5b45d24ccdc03b559e69adf40e9e77cfd8c5a54c83e5db4d |
| docs/evidence/stage-01/S1-02/mutation.log | 12160 | 9a7b1dee3553a47738767ad4f43a880fa69eb7b1dbf8719ffee5320b49c0aca4 |
| docs/evidence/stage-01/S1-02/summary.md | 50739 | 307f889987bad69eb844f567990c4ace47fa805ee96019c2793aaf83a3f57d80 |
| docs/evidence/stage-01/S1-02/handoff.md | 36687 | 3480d25543ccf5a438bd58e553490ced83bc11e527b3fdb28640f6d79b177d07 |
| docs/evidence/stage-01/S1-02/audit.md | 23283 | 80ee21d98ed0877e2a3469a3ae9389f3ddc19663a4bf569bd39a25362ee3c8f8 |
| docs/evidence/stage-01/S1-02/audit-round2.md | 32835 | 3e2475309e154b3071269f4487ecd4ba28993c84cead38c7ee56e098797922f7 |
| deployment/host/dependencies.lock.json | 14075 | 4884c4ca9944ccd66be72db71dcd99b289bba119763c965a7e3b7b7a961fac6d |
| deployment/host/README.md | 6753 | 9887fe18df7f964a66725cd0ae5ee3a3bc7d37fcc58fcdbde229da3d90c7bc7a |


S1-01 旧清单 29 项，当前只有根 CMake 和两份 deployment/host 依赖说明不同，其余 **26/26** 匹配；host/common/ 与 tests/unit/common/（含 test_support.h）没有相对该基线漂移。内存去除根 CMake 两行 storage add_subdirectory 后为 3871 B、SHA-256=25d9ca8c06e859a8b8ab85146b15bde3c740aaef215abd71d24d23a4b2db55f7，与 S1-01 相同。共享说明已按本轮允许的 N4 范围核对，没有借此修改/重开 S1-01。无 Git，不能重建完整历史 diff 或证明所有旧测试逐断言均未删；本轮自己的写入边界由初末指纹证明。

### 6.3 已保存日志：只认其历史身份

vm-s102-manifest.sha256.txt 的 32 个摘要与本轮本地匹配，source-verify 有 32 行 OK；run-summary 的 run_id=20261024-114250，声明 configure/build/CTest 与两种 sanitizer 退出码均 0，ASAN_REPORTS/TSAN_HITS=0。原 CTest 日志分别为 VM 普通/ASan/TSan 12/12，0.38/0.61/1.22s；WSL 普通/ASan/TSan 12/12，0.28/0.53/1.34s。26 个 .log 均已读取；普通/名为 sanitizer 的 build 日志未见编译 warning/error 行。

两环境 unit-tests 日志都列 11 个二进制，common 97/5/53/164/133/53=505；storage 80/28/603/131/90=932；合计 **1437**，与报告总数相同。CMake 是 6 common + 5 storage + 1 Python 漂移测试，解释器可用时共 12 个 CTest；断言计数不是源码 TEST_CHECK 站点数。上述数字来自文件核对，不是本轮跑出了 1437 次断言。

mutation.log 实有 16 项，14 项记失败，m6/m9 记通过；m9b 明细是 4/588，与当前 summary 相符。七个 SOURCE_FINGERPRINTS 前缀均匹配当前文件，但日志未附全部变异后源码/逐项构建完整命令或独立可执行脚本；前缀一致只能支持其声称的输入身份，不能单独证明执行过程。m15 的具体错误已被 P3 证伪为有效定向覆盖，其他 C 变异本轮 NOT_RUN。普通与 sanitizer 构建日志均没有实际 -fsanitize 编译/链接行，本轮不能从文件名和成功汇总独立验证插桩或 VM 当前状态。

## 7. 残留、范围与交接决定

本轮检查和探针均在内存执行，没有创建临时脚本、DB/socket、构建目录、mock、调试输出文件或安装包；正式报告是唯一写入。Python 全带 -B。源码范围未见本轮新生成 .pyc/.o/.a/.obj/.exe/.db/CMakeCache 或备份残留；已有 build/、正式测试、历史诊断/证据及第三方源码均未删除。VM/WSL 不可访问，其原有临时产物归属/清理本轮 NOT_RUN，不声称远端已清理。

本轮已检查文档同步需要：N-1–N-3 列出当前过期/错误正文。按“仅写 audit-round3.md”约束，交由开发/集成负责人在整改时同步，不越权改 summary/handoff、README、依赖锁或状态索引。

交接条件：修复 B-1/B-2，补有合法对照和完整状态断言的正式回归；修正 m15/NUL 类别覆盖与说明；在可用 Linux 对最终快照重新 configure/build/CTest 及相应 sanitizer/定向探针，保存指纹和真实退出码，再交独立复审。FAIL 时不得 DONE、不得将阶段1 gate 设 PASS、不得自动跨卡。

**写入边界核验**：写入前重算初始 1598 个文件，1598/1598 原字节摘要不变、无新增/删除；写入后再次核对，仅新增本文件，1598 个原文件仍全部匹配。未修改源码、测试、契约、旧日志/审计、状态或其他文档；未执行 git commit/push。
