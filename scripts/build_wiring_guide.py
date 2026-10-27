"""Build the reviewable wiring atlas, wire schedule and raster previews.

Drawing primitives are shared by SVG and Pillow; no inferred board footprint.
Run with the bundled Python runtime (Pillow) or another Python with Pillow.
"""
from pathlib import Path
import csv
import html

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/hardware/接线图册'
INK='#172d46'; BLUE='#176da6'; RED='#c34b35'; GREEN='#237c6a'; GRAY='#536477'; PALE='#eef4f8'; AMBER='#a16716'
FONT='C:/Windows/Fonts/msyh.ttc'
W,H=1600,1120

class Sheet:
    def __init__(self, n, title, subtitle, raster=True):
        self.n=n; self.title=title
        self.im = self.d = None
        if raster:
            from PIL import Image, ImageDraw, ImageFont
            self.font = ImageFont.truetype
            self.im=Image.new('RGB',(W*2,H*2),'white');self.d=ImageDraw.Draw(self.im)
        self.svg=[f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}"><title>{html.escape(title)}</title><rect width="1600" height="1120" fill="white"/>']
        self.rect(0,0,W,12,INK);self.text(50,35,f'{n:02d}  {title}',36)
        self.text(50,91,subtitle,19,GRAY)
        self.line([(50,135),(1550,135)],'#d6e1e9',2)
        self.text(50,1077,'2026-10-23 · 候选施工设计 / UNVERIFIED · 按丝印和实测确认后接线 · 未实施产品固件',17,GRAY)
        self.text(1370,1077,f'{n:02d} / 14',18)
    def circle(self,x,y,r,color):
        if self.d: self.d.ellipse(((x-r)*2,(y-r)*2,(x+r)*2,(y+r)*2),fill=color)
        self.svg.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>')
    def rect(self,x,y,w,h,fill=PALE,stroke=None):
        if self.d: self.d.rectangle((x*2,y*2,(x+w)*2,(y+h)*2),fill=fill,outline=stroke,width=2)
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="5" fill="{fill}" stroke="{stroke or fill}"/>')
    def text(self,x,y,s,size=22,color=INK):
        if self.d: self.d.text((x*2,y*2),s,font=self.font(FONT,size*2),fill=color)
        self.svg.append(f'<text x="{x}" y="{y+size}" font-family="Microsoft YaHei,sans-serif" font-size="{size}" fill="{color}">{html.escape(s)}</text>')
    def lines(self,x,y,ss,size=22,color=INK,gap=34):
        for i,s in enumerate(ss):self.text(x,y+i*gap,s,size,color)
    def line(self,pts,color=BLUE,width=3):
        if self.d: self.d.line([(x*2,y*2) for x,y in pts],fill=color,width=width*2)
        self.svg.append(f'<polyline points="{" ".join(f"{x},{y}" for x,y in pts)}" fill="none" stroke="{color}" stroke-width="{width}"/>')
    def box(self,x,y,w,h,title,lines=(),color=BLUE):
        self.rect(x,y,w,h,PALE);self.rect(x,y,5,h,color);self.text(x+20,y+12,title,25,color)
        self.lines(x+20,y+56,lines,20,gap=28)
    def row(self,y,left,right,label='',color=BLUE):
        self.rect(65,y,560,45,PALE);self.rect(965,y,570,45,PALE)
        self.text(78,y+6,left,21);self.text(978,y+6,right,21)
        self.line([(630,y+24),(960,y+24)],color,3)
        if label:self.rect(672,y-2,246,26,'white');self.text(678,y-3,label,16,color)
    def table(self,x,y,width,headers,rows,colwidths=None,rh=43,size=20):
        cols=colwidths or [width/len(headers)]*len(headers)
        xx=[x]
        for c in cols:xx.append(xx[-1]+c)
        self.rect(x,y,width,rh,INK)
        for i,h in enumerate(headers):self.text(xx[i]+12,y+8,h,size,'white')
        for j,row in enumerate(rows):
            yy=y+(j+1)*rh;self.rect(x,yy,width,rh,'#f0f5f8' if j%2==0 else '#fafcfd')
            for i,val in enumerate(row):self.text(xx[i]+12,yy+8,str(val),size)
    def save(self):
        name=f'{self.n:02d}-{self.title}'
        (OUT/(name+'.svg')).write_text('\n'.join(self.svg)+ '</svg>',encoding='utf-8')
        if self.im: self.im.save(OUT/(name+'.png'))
        return name

def build_overview(raster=False):
    s=Sheet(1,'全系统位置与走线','先安排位置，再按 02–06 页逐线连接；这是安装示意，不是按比例加工图。', raster=raster)
    s.rect(55,165,960,690,'#f7f9fb','#cdd9e2')
    s.text(80,180,'柜内正视 · 刚性绝缘安装背板',25)
    s.box(90,240,260,130,'C01 USB 相机',['上后方朝工具区','随附 USB 线 → RK HOST'],GREEN)
    s.box(390,240,250,130,'C02 白光补光',['侧上方斜照，避反光','5V USB → 独立充电口'],GREEN)
    s.box(700,240,270,130,'B04 门磁',['带线件固定柜框','磁铁固定柜门'],BLUE)
    s.box(90,430,260,150,'天空星 F407',['P1 / P2 → 信号线','Type-C → 5V USB','距锁与强电回路留距离'])
    s.box(390,430,250,150,'B07 ADuM1201',['MCU 侧 / RK 侧分开','不要跨隔离接地','短线、固定、可测'])
    s.box(700,430,270,150,'RK3568 + 原配屏',['用原配电源和屏线','相机接 USB HOST','网口 → 路由器'],GREEN)
    s.box(90,645,260,145,'分压 / 保护小板',['3×7cm 原板先不裁','端子朝外，线可拆','元件面朝上，背面绝缘'],AMBER)
    s.box(390,645,250,145,'B03 MOS 模块',['独立固定，端子朝外','不要硬焊到洞洞板','锁功率线走另一侧'],RED)
    s.box(700,645,270,145,'B02 锁体 + 锁扣',['柜门边缘，刚性支撑','保留尾杆手动通道','红黑 22AWG → MOS'],RED)
    s.line([(348,500),(390,500)],BLUE);s.line([(640,500),(698,500)],GREEN)
    s.line([(520,790),(520,825),(825,825),(825,790)],RED,5)
    s.box(1055,165,490,185,'柜门 / 工具区',['B01 RC522：门外非金属刷卡面','B05 微动：工具托架下，限定行程','B20 蜂鸣器：开孔朝外，避相机','门铰链线束留弯，不能夹线'])
    s.box(1055,380,490,215,'线材与固定',['锁：22AWG 铜软线 + 螺丝端子','信号：20cm 母对母杜邦线','无排针器件：焊接后套热缩管','板底尼龙柱架空，不能贴裸金属','线两端贴同一线号；先量后剪'])
    s.box(1055,625,490,220,'电源分配',['12V/2A → 保险 → MOS → 锁','5V USB → F407 Type-C','RK：只用原配电源','灯：独立 USB 充电口，核总电流','外壳内只走低压成品适配器输出'],RED)
    s.lines(75,890,['布线：锁电源成对并行或轻绞；SPI 短线，先以低速调试；ADC 靠 MCU、远离 MOS 和锁线。','安装距离先以 5cm 分区作为起点，最终以关门、开锁、相机画面和干扰实测调整。','现有 1m 锁电源线须量路由总长；若不够，报告补线成本，不拉紧、不拼杜邦线。','锁扣、微动和门铰链依赖实物尺寸；无手电钻时优先复用带孔刚性支撑，不在纸箱上承力。'],21,gap=38)
    return s
    


def main():
    OUT.mkdir(exist_ok=True)
    names=[build_overview(raster=True).save()]
    s=Sheet(2,'核心板每孔索引','P1 / P2 / H1 名称按官方原理图；下面是连接器编号视图，不是已核实的实物正反面照片。')
    p1=['PA2','VREF_EXT','PA0','PA1','PA4','PA3','PA6','PA5','PC4','PA7','PB0','PC5','PE7','PB1','PE9','PE8','PE11','PE10','PE13','PE12','PE15','PE14','PB10','PB11','PB13','PB12','PB15','PB14','PD9','PD8','PD11','PD10','PD13','PD12','PD15','PD14','AGND','3V3','GND','5V_IN']
    p2=['PD3','PC9','PC8','PC12','PD2','PC11','PC10','PC13','PE6','PE5','PE4','PE3','PE2','PC0','PC1','PC2','PC3','PE1','PE0','PB9','PB8','PB7','PB6','PB5','PB4','BAT_EXT','PD7','PD6','PD5','PD4','PD1','PD0','PA15','PA8','PC6','PC7','GND','3V3','GND','5V_IN']
    # Official P2 PE0 is pin19; use this index consistently throughout atlas.
    used={'PB13':'SCK','PB12':'CS','PB15':'MOSI','PB14':'MISO','PE3':'RST','PE2':'蜂鸣','PC0':'ADC','PE1':'微动','PE0':'门磁','PB6':'锁','PD5':'TX','PD6':'RX'}
    for x,p,label in [(65,p1,'P1 左侧排针'),(605,p2,'P2 右侧排针')]:
        s.text(x,159,label,26)
        for r in range(20):
            yy=208+r*36
            for c in range(2):
                idx=r*2+c;pin=p[idx];xx=x+c*250
                col=BLUE if pin in used else GRAY
                s.rect(xx,yy,240,32,'#e2f0fb' if pin in used else '#f4f6f8')
                s.text(xx+7,yy+4,f'{idx+1:02d}  {pin}'+(' · '+used[pin] if pin in used else ''),18,col)
    s.box(1150,160,390,300,'H1 调试排针',['01 SWO      02 3V3','03 SWCLK    04 GND','05 SWDIO    06 GND','07 5V_IN    08 PA10 / RX','09 NRST     10 PA9 / TX','只用 03/05/04/09 做 SWD','日志：10 → CH340 RXD'],BLUE)
    s.box(1150,490,390,260,'先找 1 脚，再数孔',['每排：左奇数 / 右偶数','第 r 行：2r−1 / 2r','例如 P2-23：第12行奇数侧','不要用 USB 朝向猜正反面','先按丝印、方焊盘定位 1 脚','不确定就通断量到已知网络'],AMBER)
    s.lines(65,965,['板载 Flash：PA4=CS，PA5=SCK，PA6=MISO，PA7=MOSI，内部已连；不要再加外接飞线。','PA13/PA14 留 SWD，PA11/PA12 留 USB；PA0 按键、PB2 LED/BOOT1、TF/时钟资源保持保留。','VREF_EXT / BAT_EXT / AGND 不作为普通 3V3、GND 或 IO 使用；其余未用孔保持不接。'],20,gap=31)
    names.append(s.save())
    
    wire_rows=[
    ('S01','P1-25 · PB13 / SPI2_SCK','RC522 · SCK','黄 · 母对母'),
    ('S02','P1-28 · PB14 / SPI2_MISO','RC522 · MISO','绿 · 母对母'),
    ('S03','P1-27 · PB15 / SPI2_MOSI','RC522 · MOSI','蓝 · 母对母'),
    ('S04','P1-26 · PB12 / GPIO CS','RC522 · SDA / SS / NSS','白 · 母对母'),
    ('S05','P2-12 · PE3 / GPIO','RC522 · RST','紫 · 母对母'),
    ('S06','P1-38 · 3V3 分配点','RC522 · 3.3V','红 · 仅3.3V'),
    ('S07','MCU GND 分配点','RC522 · GND','黑'),
    ('S08','P2-19 · PE0 / EXTI0','门磁 · 带线触点任一端','信号 + 地成对'),
    ('S09','MCU GND 分配点','门磁 · 触点另一端','无极性'),
    ('S10','P2-18 · PE1 / EXTI1','微动 · NO（两线款任一端）','信号 + 地成对'),
    ('S11','MCU GND 分配点','微动 · COM（另一端）','无极性'),
    ('S12','P2-13 · PE2 / GPIO','蜂鸣器模块 · IN / S','高有效待测'),
    ('S13','3V3 分配点（先核模块规格）','蜂鸣器模块 · VCC / +','候选3.3V'),
    ('S14','MCU GND 分配点','蜂鸣器模块 · GND / −','黑')]
    s=Sheet(3,'RC522门磁微动蜂鸣器','按端子名字连接，不按模块排针顺序猜；一条横线就是一根导线。')
    for i,(num,a,b,note) in enumerate(wire_rows):s.row(160+i*51,num+'  '+a,b,note,GRAY if 'GND' in a else BLUE)
    s.box(65,894,720,150,'传感器输入各配一个 10k 上拉',['3V3 → 10k → PE0；门磁接在 PE0 与 GND 间','3V3 → 10k → PE1；微动接在 PE1 与 GND 间','设计预期：磁铁靠近 / 微动按下 = LOW，仍需实测'],BLUE)
    s.box(815,894,720,150,'其余脚与供电',['RC522 IRQ 不接；SDA 在 SPI 模式是 CS，不是 I²C','蜂鸣器是带驱动模块，不把裸蜂鸣器接 GPIO','3V3 / GND 用焊接分配点；不能多根线硬塞杜邦孔'],AMBER)
    names.append(s.save())
    
    s=Sheet(4,'MOS锁回路与分压','YYNMOS-1：按用户照片的端子名称绘图；右侧四端子的物理上下顺序必须翻看背面丝印确认。')
    s.box(60,180,310,160,'P01 / B11 电源入口',['DC 5.5×2.1 母头','先实测中心 / 外圈极性','红 = 实测 +；黑 = 实测 −'],RED)
    s.rect(650,205,300,430,'#e3f1e8',GREEN)
    s.text(666,218,'B03 YYNMOS-1',24,GREEN)
    for xx,yy,label in [(660,365,'PWM'),(660,414,'GND'),(823,251,'DC−'),(823,301,'DC+'),(823,436,'OUT+'),(823,491,'OUT−')]:
        s.rect(xx,yy,119,39,'#c2d49a',GREEN);s.text(xx+10,yy+5,label,21)
    s.lines(668,552,['按照片背面标字对应','到货须核实同版次','不要把正反面照抄'],17,gap=24)
    s.box(1170,210,360,130,'B02 锁体',['LOCK+ ← OUT+','LOCK− ← OUT−'],RED)
    s.line([(370,220),(500,220),(500,270),(815,270)],GRAY,5)
    s.line([(370,310),(460,310),(460,320),(815,320)],RED,5)
    s.text(390,194,'上路：P01− → DC−',18,GRAY)
    s.text(390,341,'下路：P01+ → 保险 → DC+',18,RED)
    s.line([(950,455),(1060,455),(1060,272),(1170,272)],RED,5);s.text(972,428,'OUT+',20,RED)
    s.line([(950,510),(1110,510),(1110,315),(1170,315)],GRAY,5);s.text(975,519,'OUT−',20)
    s.box(1170,380,360,175,'B09a 1N4007 续流',['有色环的一端 K → LOCK+','无环的一端 A → LOCK−','跨接在锁两端，尽量靠锁体','焊点分别热缩绝缘，非串联'],AMBER)
    s.box(60,405,515,215,'输入保护 / 控制',['保险后 12V+ 接 470µF(+)；12V− 接 (−)','100nF 与 470µF 并联在输入端','PB6 → PWM；10k：PWM → 模块 GND','模块 GND → MCU GND，单独信号回路','该下拉只适用于确认高电平有效的模块'],BLUE)
    s.box(60,680,695,220,'分压板：100k / 22k / 100nF',['保险后 12V+ → 100k → 节点 ADC → 22k → 地','节点 ADC → P2-14 / PC0 / ADC123_IN10','100nF 接在节点 ADC 与地之间；地 → MCU GND','12V 时节点 ≈ 2.164V；15V 时 ≈ 2.705V','公式：输入电压 = 节点电压 × 122 / 22'],BLUE)
    s.box(805,680,725,220,'两个必须分清的网络',['OUT− / LOCK− 不能额外短接到 DC− 或 MCU GND。','模块可能为低边开关：短接会绕过 MOS。','12V− 与 MCU GND：为分压采样在入口单点相连。','因此 MCU 与锁电源不隔离；MOS 光耦不构成整机隔离。','RK 地只留在 ADuM 的另一侧，不能画一条总地跨过去。'],RED)
    s.lines(65,942,['锁线：P01 → 保险 → DC±，OUT± → 锁，均用 22AWG；保险额定须实测选定，不能按 3A 占位施工。','分压按实际最高输入与实测 VREF+ 校核；例如 VREF+=2.5V 时不能照搬 15V 上限。','锁未接时先测 MOS；高有效、默认关闭、硬件单脉冲截止验证通过前，不接真实锁做动作试验。'],21,gap=32)
    names.append(s.save())
    
    s=Sheet(5,'隔离通信下载与日志','ADuM1201 芯片编号用于核对模块通断；不能把芯片脚号直接当商品排针顺序。')
    s.box(65,170,445,180,'MCU 侧：独立的地 GND1',['P2-29 / PD5 = USART2_TX (AF7)','P2-28 / PD6 = USART2_RX (AF7)','MCU 3V3 → VDD1（芯片1）','MCU GND → GND1（芯片4）'],BLUE)
    s.box(1095,170,440,180,'RK 侧：独立的地 GND2',['RK UART_RX ← VOB（芯片6）','RK UART_TX → VIA（芯片7）','RK 的匹配 IO 电源 → VDD2（8）','RK GND → GND2（芯片5）'],GREEN)
    s.box(570,170,465,180,'ADuM1201 两个反向通道',['PD5 → VIB(3) → VOB(6) → RK_RX','PD6 ← VOA(2) ← VIA(7) ← RK_TX','每侧 VDD 与 GND 间并 100nF','先核实模块已有去耦再补焊'],BLUE)
    s.line([(510,250),(565,250)],BLUE,4);s.line([(1035,250),(1090,250)],GREEN,4)
    s.rect(65,380,1470,80,'#fff4e9');s.lines(85,389,['RK 型号未知：物理针号、串口复用、设备节点、IO 电压仍待核；不是 USB / RS232 / RS485 接口。','仅当 RK UART 确认为 3.3V TTL 才使用本图 3.3V 方案；1.8V 不能直接接 ADuM1201。'],21,RED,gap=32)
    rows=[('D01  H1-03 / SWCLK (PA14)','ST-LINK · SWCLK','20cm内'),('D02  H1-05 / SWDIO (PA13)','ST-LINK · SWDIO','20cm内'),('D03  H1-04 或 H1-06 / GND','ST-LINK · GND','黑'),('D04  H1-09 / NRST','ST-LINK · NRST / RST（若引出）','可选'),('L01  H1-10 / PA9 / TX','CH340 · RXD','3.3V TTL'),('L02  H1-04 或 H1-06 / GND','CH340 · GND','黑'),('L03  H1-08 / PA10 / RX','CH340 · TXD（需输入时才接）','先测TX高电平')]
    for i,(a,b,c) in enumerate(rows):s.row(495+i*56,a,b,c)
    s.box(65,915,720,125,'避免倒供电',['F407 由 Type-C 供电；ST-LINK 的 5V 不接。','克隆 ST-LINK 的 3.3V 可能是输出：未确认前不接。','正规探头 VTref 才接 H1-02 作为参考；先看探头说明。'],AMBER)
    s.box(815,915,720,125,'隔离边界',['CH340 的 VCC 不接；USB 只接调试 PC。','调试 PC / 示波器地可能绕过隔离，接前查地路径。','不要把 GND1 与 GND2 用跳线或同一地夹连接。'],RED)
    names.append(s.save())
    
    s=Sheet(6,'小板元件脚与安装','使用原有两片 3×7cm 洞洞板；不根据未核实模块尺寸裁板或钻孔。')
    s.box(65,165,720,265,'A板：输入与分配 · 元件面朝上',['左边：12V 输入端子（+ / −）及保险后的入口','中间：470µF/25V + 100nF（并联于输入）','右边：12V 输出到 MOS DC+ / DC−','下边：MCU 地 / MOS 信号地的分配焊点','MOS 单独用尼龙柱或绝缘支撑固定在旁边','KF301 是 5.0mm 脚距；须试插，不强塞 2.54mm 孔'],RED)
    s.box(815,165,720,265,'B板：分压与低电流分配',['上方：3V3 / GND 分配焊点，各自同网','中间：100k 上臂 → ADC节点 → 22k 下臂','100nF 紧靠节点和 GND；引到 PC0 的线尽量短','下方：PE0 / PE1 各自 10k 上拉 → 3V3','每个网络标名；洞洞板相邻孔默认不导通','分配点和功率线分开；不用连片锡承载锁电流'],BLUE)
    s.table(65,475,1470,['器件','第一个端点','另一个端点','辨认与处理'],[
    ('470µF/25V','保险后12V+','12V−','壳体负极条纹=−；长脚仅新件参考'),
    ('100nF / 104（输入）','保险后12V+','12V−','无极性'),
    ('100k 上臂','保险后12V+','ADC节点','棕黑黑橙棕(1%)；以测阻为准'),
    ('22k 下臂','ADC节点','MCU GND','红红黑红棕(1%)；以测阻为准'),
    ('100nF / 104（滤波）','ADC节点','MCU GND','无极性'),
    ('10k ×2 输入上拉','3V3 分配点','PE0 / PE1（各一只）','棕黑黑红棕(1%)'),
    ('10k PWM 下拉','PWM / PB6','模块信号 GND','仅适用确认高有效的模块'),
    ('1N4007','K 有色环 → LOCK+','A 无环 → LOCK−','紧靠线圈；跨接，分别包热缩管'),
    ('端子 / 接头','按网络名标记','同名点连通','插头朝外，拧紧后轻拉检查'),
    ],colwidths=[280,300,310,580],rh=45,size=19)
    s.lines(65,970,['焊接：先电阻和瓷片，再排针和端子，最后电解；锁回路用 22AWG 铜线跨接，交叉处保持绝缘。','先放实物并标孔，再焊；背面观察会左右镜像，不按元件面坐标直接抄背面。尼龙柱高度以焊点不触底为准。'],21,gap=36)
    names.append(s.save())
    
    s=Sheet(7,'逐件断电与电压验证','工具：万用表（通断 / 电阻 / 二极管 / 直流电压）、螺丝刀、放大镜；以下按顺序操作。')
    tests=[
    ('01 板卡和每个端子','断开全部电源与USB，目检丝印','核对 STM32F407VGT6 / PRO；P1/P2/H1 的1脚标记','版本不符停；不以图片朝向猜孔位'),
    ('02 电阻与焊接','电阻档，器件焊前各量两端','100k / 22k / 10k 在标称容差内；每线两端通断','在电路内读值受并联影响，必要时抬起一端'),
    ('03 二极管','二极管档，红笔 A、黑笔 K 有环端','1N4007 正向通常约0.5–0.8V；反向 OL','断电、脱离线圈测；双向近0停'),
    ('04 门磁 B04','通断档，两表笔接两根线','磁铁靠近应闭合，移远应断开；记录动作距离','与MCU分离测；磁铁件没有导线'),
    ('05 微动 B05','通断档，两表笔接两线 / COM与NO','松开断；按下通；三脚款另找 COM/NC','线色不能识别 NO/NC；避免压到底损坏'),
    ('06 MOS 断电','端子到芯片/铜箔的通断与目检','逐个辨 DC±、OUT±、PWM/GND；记录两地是否通','OUT−与DC−不通可能正常；不能据此补短接'),
    ('07 12V适配器','DC V档，红笔中心、黑笔外圈','读正值约12V才定义中心为正；记录空载电压','负读数说明极性相反；先不接核心板'),
    ('08 核心板电源','只接Type-C；黑笔H1-04，红笔H1-02','约3.3V；H1-07对同地约5V；不发烫','不向3V3送5V；12V不接任何MCU引脚'),
    ('09 分压板脱离ADC','供12V；黑笔分压地，红笔ADC节点','约2.164V；实际输入×22/122，误差看阻值','先量节点再接PC0；必须低于实测VREF+'),
    ('10 输入电平','上拉就位；黑笔GND，红笔PE0/PE1','开路约3.3V，闭合约0V；记录门关/有物极性','输入不得悬空；读值异常先断开触点查上拉'),
    ('11 RC522 / 蜂鸣器','黑笔模块GND，红笔VCC / 3.3V脚','模块处约3.3V；IRQ悬空；蜂鸣器型号需匹配','RC522 不试5V；蜂鸣器3.3V不兼容则停'),
    ('12 SWD / USB-TTL','先不接目标；TTL USB供电，量TXD对GND','TXD空闲约3.3V；跳线改VCC不等于改IO电平','约5V则不接PA10；不能只看3.3V丝印'),
    ('13 ADuM / RK','断电对模块排针逐点量到芯片1–8脚','对应05页；芯片1201，非1200；两侧不短接','RK板型、IO域未核，暂不接RK信号与电源'),
    ]
    for i,(a,b,c,d) in enumerate(tests):
        y=155+i*66;s.rect(65,y,1470,62,PALE if i%2==0 else '#fafcfd')
        s.text(78,y+5,a,19,BLUE);s.text(320,y+5,b,19)
        s.text(320,y+32,c,18);s.text(1000,y+32,d,17,RED)
    names.append(s.save())
    
    s=Sheet(8,'通电功能与硬截止验证','工具：已有示波器 / 万用表 / ST-LINK / 日志PC；需要对应验证固件，本轮不编写、不执行产品卡。')
    checks=[
    ('A 先验证 MOS，不接真实锁',[
    '锁从 OUT± 拔掉；OUT+ 与 OUT− 间接一只 10k 测试负载（12V时约0.014W）。',
    '万用表 DC V：红笔 OUT+，黑笔 OUT−。PWM=0V 和3.3V分别测，不能悬空。',
    '必须先确认输入允许3.3V；高有效候选预期：低时接近0V、高时接近输入电压。',
    '这只验证小负载开关；不证明锁电流能力。异常先断12V，核端子、极性与接线。']),
    ('B 复位默认关闭与硬件截止',[
    '示波器CH1探PWM(PB6)，地夹只夹MCU GND；CH2测OUT−对DC−（先核两地关系）。',
    '用10k假负载测上电/复位/MCU掉电默认关闭；单测MOS时先拔PC0采样线。',
    '后续固件用TIM4_CH1/AF2硬件单脉冲；测试停止RTOS进度但保持定时器时钟运行。',
    '定时器调试冻结会改变行为，先记录DBG配置；不能只用任务延时或ISR拉低当证据。']),
    ('C 最后才接锁与选保险',[
    '先取得锁允许电压/最长通电时间，设脉宽不超过该值；尾杆能操作、门保持打开。',
    '硬截止通过后再接锁；万用表红表笔插有保险的电流孔，选足量程，串入锁供电正线。',
    '严禁电流档并联电源；量完立即把表笔插回VΩ孔。普通表可能漏掉启动峰值。',
    '记录启动/保持电流、线温、释放时间；按线材/锁和保险曲线选型，不能靠保险限制脉宽。']),
    ('D 通信、刷卡与ADC功能',[
    'ST-LINK先连目标读ID；日志115200/8N1由验证固件配置；RC522读VersionReg/卡UID。',
    'Flash只经板载SPI1读JEDEC/容量，不写擦旧内容；蜂鸣器GPIO高低测试并记录极性。',
    'ADC用已知输入对比万用表；核VREF+、最长适合采样时间、滤波及标定误差。',
    'RK先确认板型/IO原理图；ADuM每侧电源相对各自地测，确认方向后做UART收发。']),
    ('E 整机复查与停止条件',[
    '相机插RK USB HOST，确认/dev/video节点、画面/焦点/固定光照；屏幕保持原配接法。',
    '试关门：不夹线、工具不误压微动、门磁只在门关到位闭合；锁与锁扣不可仅靠胶粘。',
    '普通接地示波器通道地共用：不得同时夹隔离两侧；跨隔离测量需合适差分/隔离探头。',
    '发烫、掉电重启、锁不断电、ADC超范围立即断12V；留记录，不能标阶段PASS。'])]
    for i,(title,ls) in enumerate(checks):
        y=155+i*176;s.box(65,y,1470,165,title,ls,RED if i in (1,2) else BLUE)
    names.append(s.save())
    
    from wiring_details import build_details
    names.extend(build_details(Sheet, BLUE, RED, GREEN, GRAY, AMBER, PALE))

    # Wire schedule: actual used signal wires plus power/measurement/debug circuits.
    extra=[
    ('P01','适配器正极 / DC母头实测+','保险座一端','22AWG红'),('P02','保险座另一端','MOS DC+ / 输入电容+ / 100k上端','22AWG红'),
    ('P03','适配器负极 / DC母头实测−','MOS DC− / 输入电容−','22AWG黑'),('P04','MOS OUT+','锁正 / 二极管K','22AWG红'),('P05','MOS OUT−','锁负 / 二极管A','22AWG黑'),
    ('P06','12V−入口单点','MCU GND / 分压地','独立支路'),('P07','P2-23 PB6','MOS PWM','信号线'),('P08','MCU GND','MOS GND','信号回路'),
    ('A01','100k下端 / 22k上端 / 滤波104一端','P2-14 PC0','短信号线'),('A02','22k下端 / 滤波104另一端','MCU GND','短地线'),
    ('R01','3V3','10k上拉1 → PE0','焊接'),('R02','3V3','10k上拉2 → PE1','焊接'),('R03','PWM','10k下拉 → MOS信号GND','焊接'),
    ('I01','P2-29 PD5','ADuM芯片3 VIB','先追溯模块排针'),('I02','P2-28 PD6','ADuM芯片2 VOA','先追溯模块排针'),
    ('I03','MCU 3V3','ADuM芯片1 VDD1','低电流'),('I04','MCU GND','ADuM芯片4 GND1','低电流'),
    ('I05','RK UART_RX 待定位','ADuM芯片6 VOB','RK针号未核'),('I06','RK UART_TX 待定位','ADuM芯片7 VIA','RK针号未核'),
    ('I07','RK 匹配IO电源待定位','ADuM芯片8 VDD2','仅3.3V候选'),('I08','RK GND待定位','ADuM芯片5 GND2','不跨接GND1'),
    ('D01','H1-03','ST-LINK SWCLK','信号线'),('D02','H1-05','ST-LINK SWDIO','信号线'),('D03','H1-04/06','ST-LINK GND','地线'),('D04','H1-09','ST-LINK NRST若有','可选'),
    ('L01','H1-10 PA9 TX','CH340 RXD','3.3V TTL'),('L02','H1-04/06','CH340 GND','地线'),('L03','H1-08 PA10 RX','CH340 TXD','可选先测3.3V'),
    ('U01','USB充电头5V','F407 Type-C','USB-A转C成品线'),('U02','RK USB HOST','C01相机','随附USB线'),('U03','独立5V USB口','C02补光','随附开关线'),('U04','RK原配电源','RK原配电源口','待型号核口'),('N01','RK网口','路由器LAN','Cat5e成品网线')]
    with (OUT/'逐线连接表.csv').open('w',encoding='utf-8-sig',newline='') as f:
        w=csv.writer(f);w.writerow(['线号','起点','终点','线材或限制']);w.writerows(wire_rows+extra)
    index='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>借还柜逐引脚接线图册</title><style>body{margin:0;background:#eef3f7;color:#172d46;font:17px "Microsoft YaHei",sans-serif}header,main{max-width:1500px;margin:auto;padding:24px}nav{display:flex;flex-wrap:wrap;gap:12px}a{color:#176da6}article{overflow:auto;background:white;margin:28px 0;padding:16px;box-shadow:0 4px 24px #172d4615}img{width:100%;height:auto}p{line-height:1.8}.note{border-left:5px solid #c34b35;padding:12px;background:#fff7eb}@media print{header{display:none}article{break-after:page;margin:0;padding:0;box-shadow:none}@page{size:A3 landscape;margin:8mm}}</style><header><h1>借还柜 · 逐引脚接线图册</h1><p>14张分图：09–14页为新增放大细节。先看09电源锁回路、10分压焊点、12元件极性、14表笔落点。</p><p class="note">当前是候选施工设计，未实物验收。RK板型、MOS商品端子顺序/触发、锁额定脉宽、参考电压尚待核实。不要据此直接整机上电。</p><p><a href="#09">先看新增细节图 09–14</a> · 图片可使用下方150%/200%按钮横向滚动，点击图片打开SVG。</p><nav>'''
    for name in names:index+=f'<a href="#{name[:2]}">{name}</a>'
    index+='</nav><p><a href="逐线连接表.csv">下载逐线连接表 CSV</a> · <a href="../引脚确定与接线表.md">来源、候选资源与核对说明</a></p></header><main>'
    for name in names:
        controls = ''.join(f'<button type="button" onclick="this.parentElement.nextElementSibling.querySelector(&quot;img&quot;).style.width=&quot;{width}&quot;">{label}</button> ' for label,width in [('适应窗口','100%'),('放大150%','2400px'),('放大200%','3200px')])
        index+=f'<article id="{name[:2]}"><h2>{name}</h2><div>{controls}</div><a href="{name}.svg"><img alt="{name}" src="{name}.svg"></a><p><a href="{name}.png">高清 PNG</a> · <a href="{name}.svg">矢量 SVG</a></p></article>'
    index+='</main></html>'
    (OUT/'接线图册.html').write_text(index,encoding='utf-8')
    # Replace misleading legacy overview with the same current installation diagram.
    (ROOT/'docs/hardware/系统接线图.svg').write_text((OUT/(names[0]+'.svg')).read_text(encoding='utf-8'),encoding='utf-8')
    print(f'Generated {len(names)} sheets (SVG + PNG), HTML and {len(wire_rows+extra)} wire rows.')
    


if __name__ == "__main__":
    main()
