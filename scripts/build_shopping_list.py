"""Render a mobile shopping list from the BOM plus Taobao search keywords.

No network, no ordering. Links are search entries or already-verified item pages.
"""
from __future__ import annotations

import json
import urllib.parse
from decimal import Decimal
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "docs/hardware/手机选购清单.html"

CSS = """
:root{--bg:#f6f7f9;--card:#fff;--ink:#1a1a1a;--sub:#6b7280;--line:#e5e7eb;--accent:#e4572e;--jd:#c8102e}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.5 -apple-system,BlinkMacSystemFont,"PingFang SC","Microsoft YaHei",sans-serif}
header{position:sticky;top:0;background:#fff;border-bottom:1px solid var(--line);padding:12px 16px;z-index:9}
h1{margin:0;font-size:17px}
.meta{margin-top:4px;font-size:13px;color:var(--sub)}
main{padding:12px 12px 40px}
h2{font-size:14px;color:var(--sub);margin:20px 4px 8px;font-weight:600}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:10px}
.row{display:flex;align-items:baseline;gap:8px}
.id{font-size:12px;color:#fff;background:#9ca3af;border-radius:6px;padding:1px 6px}
.id.hi{background:var(--accent)}
.name{font-weight:600;font-size:16px;flex:1}
.price{font-size:15px;font-weight:700;color:var(--accent)}
.picked{font-size:13px;color:#111;padding:6px 8px;border-radius:6px;margin:8px 0 6px}
.picked.ok{background:#f0fdf4;border-left:3px solid #16a34a}
.picked.warn{background:#fff7ed;border-left:3px solid #ea580c}
.must{font-size:13px;color:var(--sub);margin:4px 0 12px}
.kw{font:13px/1.4 ui-monospace,Menlo,Consolas,monospace;background:#f3f4f6;border-radius:8px;padding:8px 10px;word-break:break-all;color:#374151}
.btns{display:flex;gap:8px;margin-top:12px}
a.btn{flex:1;text-align:center;text-decoration:none;padding:13px 10px;border-radius:10px;font-size:15px;font-weight:600;background:var(--accent);color:#fff}
a.btn.ghost{background:#fff;color:var(--accent);border:1.5px solid var(--accent)}
a.btn.jd{background:#fff;color:var(--jd);border:1.5px solid var(--jd)}
footer{padding:0 16px 32px;font-size:12px;color:var(--sub);line-height:1.7}
"""


def render() -> str:
    bom = json.loads((ROOT / "config/materials/selected-bom.json").read_text(encoding="utf-8"))
    kw = json.loads((ROOT / "config/materials/tb-search-keywords.json").read_text(encoding="utf-8"))
    by_id = {i["id"]: i for i in bom["items"]}
    groups = kw["groups"]

    cards: dict[str, list[str]] = {g["id"]: [] for g in groups}
    for item in kw["items"]:
        bom_item = by_id.get(item["id"], {})
        q = urllib.parse.quote(item["kw"])
        search_url = f"https://s.taobao.com/search?q={q}"
        src = bom_item.get("source")
        iid = bom_item.get("item_id")
        sku = bom_item.get("sku_id")

        chosen = None
        if src == "jd" and iid:
            chosen = (f"https://item.jd.com/{iid}.html", "打开京东已选")
        elif iid and sku:
            chosen = (f"https://item.taobao.com/item.htm?id={iid}&skuId={sku}", "打开已选商品")
        elif iid:
            chosen = (f"https://item.taobao.com/item.htm?id={iid}", "打开已选商品")

        ref = item.get("ref_price")
        prefer_tb = bool(item.get("prefer_taobao")) and bool(ref) and chosen

        if chosen and prefer_tb:
            gap = Decimal(bom_item["amount"]) - Decimal(ref)
            pct = (gap / Decimal(ref) * 100).quantize(Decimal("1"))
            buttons = [f'<a class="btn" href="{search_url}" rel="noreferrer">淘宝搜索（找便宜）</a>',
                       f'<a class="btn jd" href="{chosen[0]}" rel="noreferrer">{chosen[1]} ¥{bom_item["amount"]}</a>']
            note = (f'京东 ¥{bom_item["amount"]}，旧淘宝参考 ¥{ref}，'
                    f'贵 {gap:.2f} 元（{pct}%）→ <b>建议先去淘宝找同规格便宜款</b>')
            picked_cls = "warn"
        elif chosen:
            buttons = [f'<a class="btn" href="{chosen[0]}" rel="noreferrer">{chosen[1]}</a>',
                       f'<a class="btn ghost" href="{search_url}" rel="noreferrer">比价搜索</a>']
            note = "已选：<b>" + (bom_item.get("sku_label") or "见商品页") + "</b>"
            picked_cls = "ok"
        else:
            buttons = [f'<a class="btn" href="{search_url}" rel="noreferrer">淘宝搜索（待选）</a>']
            cand = item.get("candidate")
            if cand:
                buttons.append(f'<a class="btn jd" href="{cand["url"]}" rel="noreferrer">{cand["label"]}</a>')
            note = "尚未选定商品，需你挑一个"
            picked_cls = "warn"

        hi = " hi" if item["group"] in ("missing", "verify") else ""
        cards[item["group"]].append(
            f'<div class="card"><div class="row"><span class="id{hi}">{item["id"]}</span>'
            f'<span class="name">{item["name"]}</span><span class="price">¥{item["price"]}</span></div>'
            f'<div class="picked {picked_cls}">{note}</div>'
            f'<div class="must">必须满足：{item["must"]}</div>'
            f'<div class="kw">{item["kw"]}</div>'
            f'<div class="btns">{"".join(buttons)}</div></div>'
        )

    board = bom.get("mcu_board")
    if board:
        groups = groups + [{"id": "board", "title": "⑤ 单独预算：核心板（不计入 200 上限）"}]
        cards["board"] = [
            f'<div class="card"><div class="row"><span class="id hi">{board["id"]}</span>'
            f'<span class="name">{board["name"]}</span><span class="price">¥{board["amount_cny"]}</span></div>'
            f'<div class="picked warn">单独预算，不计入 200 元上限；{board["amount_cny"]} 是参考价，未核实结算价</div>'
            f'<div class="must">购买前核对：STM32F407VGT6（不是 GD32/HC32 版）、高配版（W25Q128 已贴）、排针未焊需自己焊约 90 个点</div>'
            f'<div class="btns"><a class="btn" href="{board["url"]}" rel="noreferrer">打开官方工程页</a>'
            f'<a class="btn ghost" href="{board["doc_url"]}" rel="noreferrer">原理图/引脚图</a></div></div>'
        ]

    total = bom["totals"]["with_both_spares"]
    cap = bom["budget_cap"]
    body = [f"<h2>{g['title']}</h2>" + "".join(cards[g["id"]]) for g in groups if cards[g["id"]]]
    html = f"""<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>采购核对清单</title><style>{CSS}</style></head>
<body>
<header><h1>实验室借还柜 · 采购核对</h1>
<div class="meta">清单合计 ¥{total} / 上限 ¥{cap}（不含相机增量 ¥50.56）· {kw['updated']}</div></header>
<main>{''.join(body)}</main>
<footer>点“淘宝搜索”会用上面的关键词打开淘宝搜索；点“已选款/京东已选”直接到已核过的商品页。<br>
价格为页面参考价，不是结算价；规格不符就不要下单，回来告诉我改。<br>
总额 {Decimal(total):.2f}，余量 {Decimal(cap) - Decimal(total):.2f}。</footer>
</body></html>"""
    return html


def main() -> None:
    TARGET.write_text(render(), encoding="utf-8", newline="\n")
    print(f"rendered {TARGET.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
