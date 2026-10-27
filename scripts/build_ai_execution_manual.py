"""Render all 45 card-specific development/audit/fix/handoff instructions."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANUAL = 'docs/AI开发执行手册.md'


def rebase_links(text, origin, destination):
    def replace(match):
        link = match.group(1)
        if link.startswith(('http:', 'https:', '#', 'mailto:')):
            return match.group(0)
        path, marker, fragment = link.partition('#')
        resolved = (origin / path).resolve()
        relative = os.path.relpath(resolved, destination).replace('\\', '/')
        return '](' + relative + (marker + fragment if marker else '') + ')'
    return re.sub(r'\]\(([^)]+)\)', replace, text)


def read_json(path):
    return json.loads((ROOT / path).read_text(encoding='utf-8'))


def extract_card(task):
    text = (ROOT / task['spec']).read_text(encoding='utf-8')
    pattern = r'^#{2,3} ' + re.escape(task['id']) + r'(?=[：: ])[^\n]*\n'
    match = re.search(pattern, text, re.M)
    if not match:
        raise ValueError('Missing task heading: ' + task['id'])
    remainder = text[match.end():]
    end = re.search(r'^#{1,3} ', remainder, re.M)
    body = remainder[:end.start()] if end else remainder
    if not body.strip():
        raise ValueError('Empty task: ' + task['id'])
    return rebase_links(body.strip(), (ROOT/task['spec']).parent, ROOT/'docs/ai-prompts')


def interfaces(task_id, mapping):
    names = []
    for group, key, fields in [
        ('internal_ports','name',['implementation','refinement']),
        ('public_routes','operation_id',['implementation','core','client']),
        ('uart_requests','name',['host','mcu_transport','mcu_semantics','real_link']),
        ('local_methods','method',['implementation']),
        ('vision_worker_messages','name',['implementation','producer','consumer']),
    ]:
        for item in mapping.get(group, []):
            assigned = [field for field in fields if task_id in (item.get(field, []) if isinstance(item.get(field), list) else [item.get(field)])]
            if assigned:
                names.append(f"{group}: {item[key]}（归属：{'/'.join(assigned)}）")
    return '\n'.join(names) or '本卡没有新增对外接口；消费既有契约，模块私有函数依任务卡实现，不能补造公共入口。'


def render_card(task, next_task, plan, mapping):
    card = task['id']
    stage = task['stage']
    directory = f'docs/evidence/stage-{stage:02d}/{card}'
    dependencies = '\n'.join(f"- {dep}: docs/evidence/stage-{int(dep[1]):02d}/{dep}/summary.md、handoff.md、audit.md" for dep in task['depends_on']) or '- 无前置实施卡；仍需检查当前工作区、契约和本卡运行环境。'
    stage_data = next(s for s in plan['stages'] if s['id'] == stage)
    gate = stage_data['gate_task'] == card
    next_label = next_task['id'] + ' ' + next_task['title'] if next_task else '无后续卡：S5-07完成最终交付'
    scopes = '\n'.join('- ' + path for path in task['primary_paths'])
    common = f'''项目根目录：%REPO%。
本卡：{card} {task['title']}；契约基线：{plan['contract_version']}。
执行前从contract-source读取实际当前版本，版本不符先核对，不能静默使用旧副本。
先读AGENTS.md、README.md、docs/stages/README.md、config/development/stage-plan.json，
再读{task['spec']}中本卡、阶段公共约束/验收/附录；定向读traceability、module-ownership和本卡引用契约。
前置证据：
{dependencies}
已配置STM32环境见firmware/README.md；它不代替Linux/Android/NPU或真机验收。
主机端默认复用用户已有VMware Ubuntu，不要求WSL或新装Ubuntu；读取docs/development/vm-ubuntu-workflow.md。
Windows源主副本向VM本地Linux文件系统传固定指纹快照，保留历史日志；不得把WSL结果改名成VM结果。
VM使用独立$HOME/locker-workspace：src只放源码，build/runtime/logs/artifacts与src同级；编译、DB/socket和日志禁止写回src或旧共享源目录。
构建前按config/development/host-environment.json和VM的WORKSPACE-INDEX.md确认实际快照路径/指纹；不要把旧顶层src误作当前整改版，活跃卡的源码不覆盖。
各卡构建与日志分别置build/{card}/批次、logs/{card}/批次；归档artifacts，必要正式证据再回传Windows该卡目录。
禁止在VM的HOME根目录生成脚本、日志、安装包或模型产物；资料分类见docs/development/vm-home-layout.md。
S1-01已有实现和独立审计，先按docs/stages/S1-01-current-handoff.md核对最新证据并收尾；指纹/输入未变且证据匹配时复用已有结果，不重做环境或基础库。
原HAL、锁定vendor、E:/equipment-vision、硬件未验证模板保持既定边界，不为本卡重建另一套框架。
'''
    development = common + f'''
本次只开发{card}，不自动开始{next_task['id'] if next_task else '其他任务'}。
先核对已有代码/残留/前置门禁；确认没有其他AI同时写本卡目录或共享文件。
由本轮唯一集成负责人维护active_task及本卡真实状态，审计AI不并发改状态。
用简短清单报告目标、输入接口、修改路径和验收命令，然后持续完成本卡必要工作。
新模块先查官方/GitHub复用及许可/版本/适配；不得删失败测试或通过空成功、假数据过关。

主要修改范围（精确限定、正式测试/模块README权限以原卡为准）：
{scopes}
根构建、共享DDL、contracts及生成器各由唯一负责人维护；无本卡授权的共享修改先提交CR。
原卡明确允许回修其他模块时先报告最小缺陷与责任，不以此扩写未来阶段。

本卡实现/消费/扩展的接口定位：
{interfaces(card, mapping)}

本卡验收依据全文如下，不得缩减：
{extract_card(task)}

完成实现后运行本卡真正的构建/测试，并运行适用设计检查：
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
以上脚本只证明设计一致性，不能代替CMake/CTest、Keil、Android、UART/相机/锁实测。
环境/硬件不具备写BLOCKED和NOT_RUN，说明哪些部分已做，不伪造PASS或前置证据。

写入{directory}/summary.md和{directory}/handoff.md，附实际命令/退出码/日志索引。
handoff说明函数调用、参数/内存/线程/销毁、配置/依赖/接口版本、失败路径、复现方法和未实现项。
清理本轮明确临时残留，保留正式测试/fixture/证据，核对当前文档与实际实现。
独立复核前保持IN_PROGRESS，交付后停止，不生成虚假audit.md，不commit/push。
'''
    audit = common + f'''
你作为独立审计AI，只审{card}；不接着实现后续卡。
读取{directory}/summary.md、handoff.md、变更清单、实际源码和正式测试。
不能仅凭开发AI自述或报告文件存在认定通过；核对输入版本和前置证据。
逐条对照本卡原文的范围、产物、接口、错误/超时/重复/重启、资源上限及验收。
检查内存/线程/回调生命周期、权限/持久化/副作用边界，确认没有公共调试旁路或固定成功。
环境允许时独立执行相关真实构建和测试，记录完整命令/退出码；不能运行写NOT_RUN。
仅写{directory}/audit.md，不改业务、契约、验收预期、其他卡或stage-plan。
每个问题给文件/行号、触发/复现、影响、修复建议和阻塞级别，不造范围外问题拖延交付。
报告阻塞项、非阻塞项、未验证项、真实测试、接口漂移及是否满足交接条件。
静态设计、模拟程序、真实硬件分开评价，不把前轮PASS移植为本轮证据。
'''
    fixes = common + f'''
你是原开发AI，继续整改{card}，问题来源为{directory}/audit.md。
先逐项核对并复现阻塞项，再做最小修复；记录问题→文件→修改→回归证据。
更新summary.md/handoff.md，保留此前失败证据，不删除/放宽正式测试以改变结论。
共享契约矛盾提交CR，由架构负责人统一改真源/生成物/消费者后再继续依赖部分。
可完成不受阻塞影响的工作；范围外建议单列，不擅自加入当前卡。
重跑本卡与受影响回归/设计检查；硬件或环境不足仍保留NOT_RUN/BLOCKED。
等待同卡审计复核，不开始下一卡，不伪造独立audit或修改审计结论，不commit/push。
'''
    if gate:
        closing = f'''本卡是阶段{stage}正式门禁卡。逐项核实本阶段全部卡报告和必要实跑。
只有全部通过才生成docs/evidence/stage-{stage:02d}/summary.md汇总并链接每卡证据，
由唯一负责人同步本卡DONE、阶段implementation_status=DONE、gate_result=PASS及固定evidence。
关键NOT_RUN/阻塞存在不能把阶段设PASS，也不能启动下一阶段。'''
    elif card == 'S2-08':
        closing = '''本卡只是2A基础子门禁。写stage-02/foundation-summary.md，不能把整个阶段2设PASS。
S2-09..16未完成时完整阶段2仍在进行；通过后只解锁S2-09。'''
    else:
        closing = '本卡不是阶段正式门禁，不能因本卡DONE把整个阶段设PASS。'
    next_instruction = f'''下一张计划卡为{next_task['id']}，规格文件{next_task['spec']}。
将本卡函数/配置/构建/测试/限制交给下一AI，要求下一AI核对它的全部前置，而非只看本卡。
本次收尾后停止；用户单卡授权不自动执行下一卡。''' if next_task else '''已到最后S5-07，无下一张卡；最终报告说明交付、限制与维护方式。
recommended_next保留S5-07作为最后里程碑，不填机器索引不存在的新卡，不能自动开新项目。'''
    handoff = common + f'''
你是本轮唯一集成负责人，收尾{card}；依据本卡真实验收与{directory}/audit.md处理。
仍有阻塞或关键NOT_RUN时回开发整改/标BLOCKED，不能直接DONE。
满足条件才同步本卡DONE、report={directory}/summary.md、active_task=null及真实阶段状态。
同步README/模块README、summary/handoff、依赖/接口版本及正式测试结果。
handoff必须包含下一AI可直接运行的命令、接口调用例、生命周期/失败边界及未实现项。
{closing}
{next_instruction}
运行适用检查，报告结果与残留自查，不commit/push。
'''
    result = f"# {card}：{task['title']}\n\n前置：{'、'.join(task['depends_on']) or '无前置实施卡'}。下一张计划：{next_label}。\n\n"
    result += f"[原始卡与公共约束](../../{task['spec']}) / [总手册](../AI开发执行手册.md)。本文件含4份已填写卡号的指令，按执行时机分别复制，不一次发送4份。\n\n"
    for title, body in [('开发指令',development),('独立审计指令',audit),('审计整改指令',fixes),('收尾与下一卡交接指令',handoff)]:
        result += f'## {title}\n\n````text\n{body.strip()}\n````\n\n'
    return result


def artifacts():
    plan = read_json('config/development/stage-plan.json')
    mapping = read_json('config/development/interface-map.json')
    tasks = plan['tasks']
    results = {}
    index = ['# 全部任务卡AI指令索引','',f"契约基线{plan['contract_version']}；共{len(tasks)}张卡，每卡4份指令，共{len(tasks)*4}份。生成文档不改变实施状态。",'',
        '[完整执行手册](../AI开发执行手册.md) / [通用交接流程](../AI开发交接流程.md)。','',
        '| 卡号 | 目标 | 前置 | 完整指令 |','|---|---|---|---|']
    all_cards = []
    for n, task in enumerate(tasks):
        content = render_card(task, tasks[n+1] if n+1 < len(tasks) else None, plan, mapping)
        path = f"docs/ai-prompts/{task['id']}.md"
        results[path] = content
        index.append(f"| {task['id']} | {task['title']} | {'、'.join(task['depends_on']) or '无'} | [开发/审计/整改/交接]({task['id']}.md) |")
        # Inline copy uses paths relative to docs/, not docs/ai-prompts/.
        inline = rebase_links(content, ROOT/'docs/ai-prompts', ROOT/'docs')
        inline = re.sub(r'^(#{1,2}) ', lambda m: '##' + m.group(1) + ' ', inline, flags=re.M)
        all_cards.append(inline)
    index += ['', '维护：只改正式任务卡/机器归属和通用流程，再运行python -B scripts/build_ai_execution_manual.py；不手改生成的单卡指令。','']
    results['docs/ai-prompts/README.md'] = '\n'.join(index)
    flow = (ROOT/'docs/AI开发交接流程.md').read_text(encoding='utf-8')
    flow = re.sub(r'^(#{1,2}) ', lambda m: '##' + m.group(1) + ' ', flow, flags=re.M)
    rows = '\n'.join(row.replace(f'](S', '](ai-prompts/S') for row in index if row.startswith('|'))
    intro = f'''# AI开发完整执行手册

本手册包含全部{len(tasks)}张任务卡及{len(tasks)*4}份已填卡号的开发、审计、整改、收尾指令。
每卡给出真实前置、修改范围、接口定位、原卡验收、固定交付路径与下一卡。
只需找到当前卡，复制其当前步骤的一个指令块；不用自己替换卡号或拼接架构文件。

生成内容来自正式阶段卡、stage-plan、interface-map与通用交接流程；契约基线{plan['contract_version']}。
这份手册没有启动产品开发或修改任何卡状态。STM32环境测试完成不等于产品卡完成。

## 使用顺序

开发指令→开发报告→独立审计指令→必要时整改指令→复核→收尾交接指令→下一卡。
默认串行，普通实现/回归在当前卡授权内持续完成；需要连续阶段时按通用流程明确授权。
同一时间由唯一集成负责人写状态/根构建/共享DDL/契约；独立审计只读业务。

## 45卡导航

{rows}

## 通用操作与验收方法

{flow}

## 全部卡片的可复制执行指令

以下每卡4份指令互相独立。Ctrl+F搜索“# S2-11”等卡标题，或使用上表单卡文件。

'''
    results[MANUAL] = intro + '\n'.join(all_cards)
    fingerprint_files = sorted({t['spec'] for t in tasks} | {'config/development/interface-map.json','docs/AI开发交接流程.md',
        'config/development/host-environment.json','docs/development/vm-ubuntu-workflow.md','docs/stages/S1-01-vm-final-review.md','docs/stages/S1-01-current-handoff.md','docs/development/vm-home-layout.md'})
    # Do not hash mutable card statuses; current version/IDs/dependencies are checked via rendered output.
    manifest = {'contract_version':plan['contract_version'],'cards':[t['id'] for t in tasks],'instructions_per_card':4,
        'instruction_count':len(tasks)*4,'source_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in fingerprint_files}}
    results['docs/ai-prompts/manifest.json'] = json.dumps(manifest,ensure_ascii=False,indent=2)+'\n'
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    outputs = artifacts()
    for relative, content in outputs.items():
        target = ROOT/relative
        if args.check:
            if not target.exists() or target.read_text(encoding='utf-8') != content:
                raise AssertionError('Execution instructions drift: ' + relative)
        else:
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_text(content,encoding='utf-8')
    manifest = json.loads(outputs['docs/ai-prompts/manifest.json'])
    print(json.dumps({'status':'PASS','mode':'CHECK' if args.check else 'GENERATE','cards':len(manifest['cards']),'instructions':manifest['instruction_count'],'files':len(outputs),'product_tasks_started_by_generator':0},ensure_ascii=False))


if __name__ == '__main__':
    main()
