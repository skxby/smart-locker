"""Render the current selected BOM; does not search, order, or verify hardware."""
from __future__ import annotations

import json
from decimal import Decimal
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def render() -> str:
    bom = json.loads((ROOT / "config/materials/selected-bom.json").read_text(encoding="utf-8"))
    total = sum((Decimal(i["amount"]) for i in bom["items"] if i["included"]), Decimal(0))
    spare_lock = next(Decimal(i["amount"]) for i in bom["items"] if i["id"] == "B23")
    require_total = Decimal(bom["totals"]["with_both_spares"])
    if total != require_total:
        raise ValueError("BOM total drift")
    kinds = {
        "CANDIDATE_PRICE_FROM_UPDATED_LIST": "沿用候选报价，未复核结算",
        "CANDIDATE_PRICE_BAND": "候选价区间，未核价",
        "BUDGET_ALLOWANCE": "补购预留，未核价",
        "OWNED_REUSE_PENDING_MATCH": "复用，待匹配确认",
        "OWNED_REUSE_CONFIRMED": "已确认复用，不采购",
        "PRODUCT_SELECTED": "已选定商品，未下单",
        "PAGE_QUOTED": "页面报价，未结算",
        "VERIFIED": "已核规格+SKU",
        "VERIFIED_SINGLE": "已核，单一SKU",
        "VERIFIED_WITH_NOTE": "已核规格（含替代做法）",
    }

    status_text = {
        "OWNED_NO_PURCHASE": "",
    }

    def source_cell(item: dict) -> str:
        link = item.get("link_markdown", "—")
        search = item.get("search_url")
        status = item.get("link_status")
        if search:
            return f"[搜索入口]({search})"
        if status in status_text:
            return status_text[status]
        if status == "VERIFIED_WITH_NOTE":
            return link if link and link != "—" else "见规格说明"
        if status == "SKU_OK_SPEC_TODO":
            return (link + " **SKU已选，产品参数待核**") if link and link != "—" else "**SKU已选，产品参数待核**"
        if status == "SPEC_MISMATCH":
            return (link + " **规格不符，待重选**") if link and link != "—" else "**规格不符，待重选**"
        if status == "NEEDS_SKU":
            return (link + " **待补SKU**") if link and link != "—" else "**待补SKU**"
        if status == "NEEDS_RELINK":
            return (link + " **待换店**") if link and link != "—" else "**待换店**"
        if status == "NOT_APPLICABLE":
            if item["price_kind"] in ("BUDGET_ALLOWANCE", "PAGE_QUOTED"):
                return "预算/运费项，无商品链接"
            return "无需采购（复用/板载/不采购）"
        return link

    increment = bom["vision_increment"]
    inc_total = sum((Decimal(i["amount"]) for i in increment["items"] if i["included"]), Decimal(0))
    if inc_total != Decimal(increment["total_cny"]):
        raise ValueError("vision increment total drift")
    if inc_total > Decimal(increment["budget_cap_cny"]):
        raise ValueError("vision increment exceeds its own cap")
    out = [
        "# 材料清单与搜索比价", "",
        "日期：2026-10-20。配置：一格柜、一件固定工具，STM32采用HAL。当前材料源是 `config/materials/selected-bom.json`，本文件由 `scripts/build_materials_doc.py` 生成。",
        "",
        "新增现金上限200元，包含运费，**不含待购F407主控板**，板卡单独预算按用户更新的清单执行。RK3568、配套屏幕与原配电源、电脑、Android手机、示波器、万用表、电烙铁和螺丝刀按现有物品复用；无手电钻。",
        "",
        "报价列沿用更新清单中的候选商品与金额，不是本轮重新逐店确认的成交价；补购项另列预留。公开搜索参考、候选链接和商家实物仍须最终核对，本轮不进行浏览器页面操作或下单。",
        "",
        f"v11 视觉采集件（相机、补光、线缆、固定、运费）是独立增量，单列于第6节，上限{Decimal(increment['budget_cap_cny']):.2f}元，**不计入上述200元**；选型理由与到货验证见[相机选型与成本](hardware/相机选型与成本.md)。",
        "",
        "## 1. 板卡与器件选型", "",
        f"候选为[立创天空星 STM32F407VGT6 高配核心板]({bom['mcu_board']['url']})，板价 49.90 元是输入清单中的参考金额，本轮未验证结算价。**该板单独预算，不计入 200 元配件上限**。",
        "",
        "- [核心板官方资源表](https://wiki.lckfb.com/zh-hans/fdb/skystar.html)列有W25Q128与32.768kHz晶振；备用电池座未贴装，赠送排针未焊接。",
        "- [DAPLink官方文档](https://wiki.lckfb.com/zh-hans/fdb/lckfb-daplink-emulator.html)描述单独配套仿真器。核心板USB接主控，不能默认含独立CMSIS-DAP/CH340，也不能默认烧入应用后自动有日志串口。",
        "- 默认预留外接SWD与USB-TTL；只有实物套装确实附带或已有器材能替代时再扣减，不额外开发USB CDC来掩盖配件缺口。",
        "- 普通高配核心板需焊赠送的外扩/SWD排针，约90个直插焊点；不是只有两块小板的16个焊点。用户已有烙铁，但耗材要核对。",
        "- 核对ST芯片、板版号和实际Flash；不要把同系列GD32板、青春版或筑基学习底板资源直接当成本板资源。",
        "",
        "## 2. 完整物料与金额", "",
        "保留两件备件，且补齐当前发现的必要配件。金额为候选报价或预算分配，单位元。",
        "",
        "| 编号 | 物料/数量 | 规格与核对项 | 金额 | 来源属性/链接 |",
        "|---|---|---|---:|---|",
    ]
    for i in bom["items"]:
        label = kinds[i["price_kind"]]
        if i["optional"]: label += "；备件可选"
        out.append(f"| {i['id']} | {i['name']} | {i['spec']} | {Decimal(i['amount']):.2f} | {label} {source_cell(i)} |")
    out += [
        "", "## 3. 预算与条件", "",
        "| 情形 | 金额 |", "|---|---:|",
        f"| 保留备用锁和备用RC522，含电源、补件、运费预留与机动 | **{total:.2f}** |",
        f"| 暂不买备用锁，其他功能材料不删 | **{total - spare_lock:.2f}** |",
        f"| 不计入上限的候选F407板卡参考价 | {Decimal(bom['mcu_board_candidate_amount_excluded']):.2f} |",
        f"| 含候选板价的总现金参考，保留两备件 | **{total + Decimal(bom['mcu_board_candidate_amount_excluded']):.2f}** |",
        f"| 视觉增量（相机/补光/线缆/固定/运费，独立上限{Decimal(increment['budget_cap_cny']):.2f}元，不计入200元） | **{inc_total:.2f}** |",
        "",
        f"保留两备件后余{Decimal(bom['budget_cap']) - total:.2f}元，可覆盖运费与少量换型差价。更稳的采购分支是不先买备用锁，留{Decimal(bom['budget_cap']) - (total - spare_lock):.2f}元缓冲；这不是删除主锁或取消任何功能。未获实际结算价前，不称200元包干。",
        "",
        "条件项仍包括：路由器无法借用约50元预留、系统存储/读卡器约25元、焊接耗材不足约10元、加工/钻孔费用按实际另计。这些不是已核价金额；出现缺口必须回算。现有RK原配电源不另买，但铭牌电压/极性仍需记录。",
        "",
        "板载LED/按键不等于板载蜂鸣器；保险座不等于保险管；电阻包不含电容；单片3×7cm洞洞板不能同时切出3×5和3×4cm两板。补件已经分行，不能再标成全部覆盖却漏算。",
        "",
        "## 4. 接线与 HAL 适配红线", "",
        "1. MC-38按当前候选NO款保留，但不依据NO/NC字样猜门状态。实测门开/关电平填profile；KW11同样实测压下/释放。API永远用CLOSED/OPEN、PRESENT/ABSENT，未校准为UNKNOWN。两线干接点不能宣称具备完整断线/短路自检。",
        "2. MOS须验证有效电平与上电默认关闭，不以‘低电平’或‘光耦’标题推断。GPIO只驱动模块输入，锁用独立12V支路，续流二极管装在线圈附近，保险与去耦不省。",
        "3. 分压按实际最大输入设计。12V示例可核对100k上臂/22k下臂，另加RC；**不能用10k上臂/22k下臂接12V**。ADC参考、源阻抗、采样时间及标定一并验证，不测市电。",
        "4. [ADuM1201资料](https://ez.analog.com/cn/interface-isolation/f/q-a/102663/adum1200-and-adum1201-supply-voltage)的两侧供电为2.7–5.5V，不直接适配1.8V UART。RK IO域以原理图与示波器确认；若为1.8V更换/增加匹配方案并重新核价。UART平均电压不能判断IO额定电平。",
        "5. 隔离板两侧电源分别来自对应域，做主机断电不反灌试验；含非隔离ADC参考连接时，不称整机完全电气隔离。锁大电流回路独立布线，信号参考地按实际电路星形安排，不让锁电流穿过MCU地线。",
        "6. USB-TTL的电源跳帽不一定改变TX IO电平，须测量/查原理图。只看MCU日志时可接MCU TX、适配器RX与GND，不把适配器VCC接目标倒供电；RK控制UART与日志UART分开。",
        "7. GPIO、ADC、UART/DMA、SPI、硬件定时脉冲全部经HAL BSP。定时器与DMA资源按实际板核对，不能占用SWD、USB或彼此冲突的管脚。",
        "8. 纸箱只是桌面样机外罩，锁和开关须固定到刚性支撑，尼龙螺丝主要固定电路板。无电钻时优先明装/复用预孔支架；不得用烙铁热剥线替代合适工具，选择剪线/剥线工具或借用。",
        "9. 首版不需要RTC电池：boot内单调时间+主机锚点足够支撑既定协议；备用电池不提供MCU整机供电，也不构成UPS。",
        "",
        "## 5. 装配与阶段门槛", "",
        "| 顺序 | 做什么 | 通过条件 |", "|---:|---|---|",
        "| 1 | 核对板型、原理图、排针、外接SWD | 能下载和断点；不要求核心板USB枚举成DAPLink |",
        "| 2 | HAL点灯/时基/日志UART，接RC522和Flash | 读卡UID支持4/7/10字节；Flash ID/几何正确 |",
        "| 3 | 独立锁支路测量 | 安全的3.3V测试源驱动模块，测电流/跌落与短脉冲，不接未知电平 |",
        "| 4 | 保护与分压两块小板 | 二极管极性、保险管、RC量程与焊接通断正确 |",
        "| 5 | 固定门磁、在位开关和锁 | 四种物理状态原始电平与profile映射一致，重复开关不松动 |",
        "| 6 | 接控制UART、隔离、ADC与告警 | 双侧IO域/掉电反灌、ADC范围、锁硬件截止实测 |",
        "| 7 | 真实持久化意图后做业务开锁 | 持久化失败不驱动，随后跑借还与尾杆非业务访问 |",
        "",
        "硬件台架/面包板属于阶段2早期诊断；阶段1仍是主机骨架与模拟器，不接实际锁。",
        "",
        "## 6. 视觉增量：相机、补光、线缆、固定", "",
        f"v11 采集件为独立增量，单独上限{Decimal(increment['budget_cap_cny']):.2f}元，不计入上表200元。选型理由、规格判据、机位与到货验证见[相机选型与成本](hardware/相机选型与成本.md)；机器清单为本文件同源 JSON 的 `vision_increment` 段。金额为候选价或预留，未结算。",
        "",
        "相机已选定 **HBVCAM-F2170HD 75°无畸变/手动调焦，￥33.76（skuId 5559151920752）**；补光选定 5V 圆形铝合金白光 3W（带开关 USB 线1米，￥9.80）。模组自带约1米 USB-A 线，固定复用尼龙柱与 EVA。用户已确认有5V USB充电头，补光由它独立供电；**充电头只能供电，不能代替RK的USB主机口给相机传数据**。7 个 SKU 的取舍见[相机选型与成本](hardware/相机选型与成本.md)。",
        "",
        "| 编号 | 物料/数量 | 规格与核对项 | 金额 | 来源属性/链接 |",
        "|---|---|---|---:|---|",
    ]
    for i in increment["items"]:
        label = kinds[i["price_kind"]]
        out.append(f"| {i['id']} | {i['name']} | {i['spec']} | {Decimal(i['amount']):.2f} | {label} {source_cell(i)} |")
    branches = increment["branches"]
    out += [
        f"| | **合计** | 计入 {Decimal(increment['budget_cap_cny']):.2f} 元独立上限 | **{inc_total:.2f}** | |",
        "",
        f"两个分支不是默认方案：极简 **{Decimal(branches['minimal_cny']):.2f}** 元（{branches['minimal_note']}）；兜底 **{branches['fallback_cny']}** 元（{branches['fallback_note']}）。",
        "",
        "不购买：MIPI CSI 模组、自动对焦模组、1080P 高带宽方案、黑白/去 IR-cut 夜视模组、工业或全局快门相机、网络摄像头、成套环形补光灯、第二只备机。商品与 SKU 已选定但未下单，未接板、未用 v4l2-ctl 实测、未标定阈值，REQUIRED 仍不可开启，S2-11 仍为 BLOCKED。",
        "",
        "## 7. 最终用户核价", "",
        f"**采购阻塞（{bom['purchase_blocker']['date']}）**：{bom['purchase_blocker']['symptom']}。影响：{bom['purchase_blocker']['impact']}。解除：{bom['purchase_blocker']['unblock']}。", "",
        "本次所有采购项均已给出具体商品页（item/SKU），页面价不等于结算价，所选SKU、套件配件、运费及电气参数仍以实际下单与实物为准。小件多店的30元运费只是预留，不是已确认包邮；同店能合并的优先合并，但不要为满减买无关件。",
        "",
        "本轮变更输入归档在 `research/2026-10-20-material-input-before-hal-review.md`，只用于追溯。当前执行以本文件、材料JSON和HAL硬件影响说明为准，不继续执行归档中的板载DAP/固定极性/错误分压说明。",
        "",
    ]
    return "\n".join(out)


if __name__ == "__main__":
    target = ROOT / "docs/材料清单与搜索比价-2026-10-20.md"
    target.write_text(render(), encoding="utf-8", newline="\n")
    print("Rendered selected BOM; candidate and allowance prices are not settled costs.")
