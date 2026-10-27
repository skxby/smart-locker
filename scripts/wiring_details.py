"""Enlarged electrical detail sheets; no unmeasured mechanical footprints."""

def build_details(Sheet, BLUE, RED, GREEN, GRAY, AMBER, PALE):
    names=[]
    def dot(s,x,y,label='',color=BLUE):
        s.circle(x,y,7,color)
        if label:s.text(x+12,y-30,label,21,color)
    def resistor(s,x,y,label):
        s.line([(x-30,y),(x,y)],BLUE,4);s.rect(x,y-17,125,34,'#eee0c3',AMBER)
        s.line([(x+125,y),(x+155,y)],BLUE,4);s.text(x-5,y-62,label,24)
    def cap(s,x,y,label):
        s.line([(x,y),(x,y+44)],BLUE,4)
        s.line([(x-28,y+44),(x+28,y+44)],BLUE,4)
        s.line([(x-28,y+62),(x+28,y+62)],BLUE,4)
        s.line([(x,y+62),(x,y+115)],BLUE,4)
        s.text(x+40,y+28,label,22)
    def meter(s,x,y,display,mode='直流电压 DC V'):
        s.rect(x,y,340,245,'#f0cf75',AMBER);s.rect(x+25,y+20,290,65,'#deebda')
        s.text(x+45,y+28,display,35);s.circle(x+170,y+142,30,GRAY)
        s.text(x+20,y+184,'COM 黑      VΩ 红',23)
        s.text(x+5,y+255,mode,23)
        return (x+65,y+230),(x+268,y+230)

    s=Sheet(9,'电源到锁完整逐端子图','图中实心圆点表示相连；只有同名网络可以连通。模块框表示端子功能，不表示内部电路。')
    s.box(65,170,295,170,'12V适配器 / 母头',['先量极性再贴标签','22AWG红：实测正','22AWG黑：实测负'],RED)
    s.box(675,170,330,550,'YYNMOS-1',['按实物背面丝印接线'],GREEN)
    ports=[('DC+',280,RED),('DC−',540,GRAY),('OUT+',360,RED),('OUT−',470,GRAY)]
    for label,y,col in ports:
        x=675 if label.startswith('DC') else 1005
        dot(s,x,y,'',col);s.text(700 if x==675 else 872,y-28,label,27,col)
    s.line([(360,270),(420,270)],RED,5);s.rect(420,248,140,44,'#fff0db',AMBER);s.text(432,253,'保险座+管',21)
    s.line([(560,270),(615,270),(615,280),(675,280)],RED,5)
    dot(s,615,280,'',RED);s.text(405,195,'保险后12V+',23,RED)
    s.line([(360,315),(385,315),(385,540),(675,540)],GRAY,5)
    dot(s,435,540,'',GRAY);s.text(80,548,'入口负极单点',23,GRAY)
    s.line([(435,540),(435,590),(100,590)],GRAY,3);s.text(100,595,'支路 → MCU GND',20,GRAY)
    s.line([(1005,360),(1445,360),(1445,400)],RED,5)
    s.line([(1005,470),(1445,470),(1445,440)],GRAY,5)
    s.rect(1375,400,145,40,'#eee0c3',AMBER);s.text(1385,404,'锁线圈',23)
    s.text(1310,318,'锁正 LOCK+',23,RED);s.text(1285,488,'锁负 LOCK−',23,GRAY)
    # Diode occupies a distinct parallel branch, never the input ground rail.
    s.line([(1175,360),(1175,390)],RED,4);s.rect(1156,390,38,55,'#292f35')
    s.rect(1156,394,38,8,'#d1d9df');s.line([(1175,445),(1175,470)],GRAY,4)
    dot(s,1175,360,'',RED);dot(s,1175,470,'',GRAY)
    s.text(1050,260,'1N4007 跨接在锁两端',23)
    s.text(1068,305,'K 有环端 → 上方红线',21,RED)
    s.text(1040,545,'A 无环端 → 下方锁负线',21,GRAY)
    s.box(1045,610,490,155,'此处没有接地线',['OUT−只到锁负与二极管A。','不能额外接DC−、MCU GND。','否则可能绕过MOS，锁持续通电。'],RED)
    # Input capacitors: the same two nets, physically near the input terminal.
    s.line([(615,280),(615,380),(470,380)],RED,4)
    s.line([(470,380),(470,400)],RED,3);cap(s,470,400,'C1')
    s.line([(470,515),(470,540)],GRAY,3);dot(s,470,540,'',GRAY)
    dot(s,590,380,'',RED);s.line([(590,380),(590,400)],RED,3);cap(s,590,400,'C2')
    s.line([(590,515),(590,540)],GRAY,3);dot(s,590,540,'',GRAY)
    s.text(420,390,'+',26,RED);s.text(420,487,'−',26,GRAY)
    s.box(65,620,550,145,'C1=470µF/25V；C2=100nF/104',['C1正脚、C2任一脚 → 保险后12V+。','C1负脚、C2另一脚 → 入口负极。','C1有极性，C2无极性；不接OUT−。'],BLUE)
    s.text(700,625,'PWM ← P2-23 / PB6',22,BLUE)
    s.text(700,664,'GND ← MCU GND',22,GRAY)
    s.lines(65,815,['电源回路：适配器+ → 保险 → DC+；适配器− → DC−；OUT± → 锁的两根线。','信号回路：PB6 → PWM，MCU GND → 模块信号GND。确认高有效后再接10k下拉。','入口负极另引支路到MCU GND，锁功率回流不经过MCU地线；分压板两地关系见第10页。','有环端是二极管阴极K，不是电解的负极标识；两种条纹含义不同，详见第12页。','保险管额定、锁脉宽与二极管电流能力仍需实测与器件规格核对。'],23,gap=42)
    names.append(s.save())

    s=Sheet(10,'分压电阻电容焊点与测点','这张图按电气节点画：每个圆点是一处连接；同一个节点允许由多个焊盘用导线连成。')
    s.text(80,185,'保险后12V+',27,RED)
    s.line([(100,350),(250,350)],RED,4);resistor(s,280,350,'R1 = 100k 上臂')
    s.line([(435,350),(720,350)],BLUE,4);dot(s,580,350,'A：采样节点')
    resistor(s,750,350,'R2 = 22k 下臂');s.line([(905,350),(1030,350),(1030,650)],GRAY,4)
    s.text(1000,688,'B：MCU GND',24,GRAY)
    s.line([(580,350),(580,460)],BLUE,4);cap(s,580,460,'C3 = 100nF / 104')
    s.line([(580,575),(580,650),(1030,650)],GRAY,4);dot(s,1030,650,'',GRAY)
    s.line([(720,350),(720,220),(1160,220)],BLUE,4)
    dot(s,720,350)
    s.box(1160,170,370,170,'MCU 采样端',['P2-14 = PC0','先拔掉此线测节点A','电压合格再断电接回'],BLUE)
    s.text(280,408,'左脚：12V+  右脚：A',22)
    s.text(750,408,'左脚：A  右脚：B',22)
    s.box(65,490,420,200,'A点必须连这四个端点',['① R1(100k)的一端','② R2(22k)的一端','③ C3(100nF)的一脚','④ 通往PC0的信号线'],BLUE)
    s.box(65,765,700,250,'用什么量、表笔在哪',['万用表黑笔COM → B（分压地）。','红笔VΩ → A（两电阻公共焊点）。','选 DC V；不是电流档，不是通断档。','PC0先不接，输入12.00V时约2.164V。','3V3、VREF+、输入范围合格后才接ADC。'],BLUE)
    s.box(805,765,725,250,'断电与回流边界',['B → MCU GND；MCU地支路 → 12V−入口。','C3不串在信号线上，两脚分别到A和B。','R1/R2无方向，但阻值和所在位置不能互换。','MCU断电时12V可能经R1/R2向ADC注入电流。','掉电兼容未验证：单独测MOS时先拔PC0线。'],RED)
    s.text(1170,440,'12V × 22 / 122',26)
    s.text(1170,490,'≈ 2.164V',32,BLUE)
    s.text(1140,585,'15V仅算例，不是已验上限。',20,RED)
    names.append(s.save())

    s=Sheet(11,'门磁微动的上拉与触点','两根触点线没有电源正负；每个输入有自己的一只10k。三脚开关按壳体COM/NO/NC认，不猜位置。')
    for x,title,pin,device in [(80,'门磁输入','P2-19 / PE0','门磁两根线'),(850,'工具在位输入','P2-18 / PE1','微动 COM 与 NO')]:
        s.text(x,170,title,30);s.text(x+60,230,'3V3分配点',24,RED)
        # Resistor is horizontal to make both leads explicit.
        s.line([(x+50,320),(x+120,320)],RED,4);resistor(s,x+150,320,'10k 上拉电阻')
        s.line([(x+305,320),(x+430,320),(x+430,450)],BLUE,4)
        dot(s,x+430,450,'输入节点')
        s.line([(x+430,450),(x+610,450)],BLUE,4);s.text(x+450,390,pin,24,BLUE)
        s.line([(x+430,450),(x+430,525),(x+340,525)],BLUE,4)
        dot(s,x+340,525);s.line([(x+340,525),(x+260,580)],BLUE,3)
        dot(s,x+340,615,'',GRAY);s.line([(x+340,615),(x+340,695),(x+590,695)],GRAY,4)
        s.text(x+50,560,device,22);s.text(x+410,655,'MCU GND',24,GRAY)
        s.text(x,750,'触点开：输入约3.3V；触点闭：约0V。',23)
    s.box(65,835,720,180,'实物接线',['门磁：任一根线到PE0，另一根到GND。','磁铁那一件不接线，靠近/远离测试触点。','微动两线NO款：任意一线PE1、另一线GND。','三脚款：COM到GND、NO到PE1，NC空着。'],BLUE)
    s.box(815,835,720,180,'表笔落点',['断电：通断档，两笔只接脱离MCU的触点。','通电：DC V，黑笔GND，红笔PE0或PE1。','别把触点接3V3/GND两端直接短接电源。','门关/工具在位对应什么电平，逐项记录。'],RED)
    names.append(s.save())

    s=Sheet(12,'元件两脚与焊接分支放大','仅表示引脚、线头和电气连接；器件外形/脚距不作为采购或裁板尺寸依据。')
    s.box(65,165,700,335,'电解电容：负极条纹 = 负脚',['470µF / 25V，有正负极。','负极条纹沿壳体，对应下方负脚。','新件长脚通常是正；剪过脚不能靠长度。'],BLUE)
    s.rect(130,350,160,95,'#263946');s.rect(250,350,22,95,'#dfe9ef');s.text(250,365,'−',22,GRAY)
    s.line([(165,445),(165,475),(350,475)],RED,4);s.text(360,451,'正脚 → 保险后12V+',23,RED)
    s.line([(255,445),(255,515),(350,515)],GRAY,4);s.text(360,495,'负脚 → 12V−入口',23,GRAY)
    s.box(815,165,720,335,'二极管：有色环 = 阴极K',['1N4007有方向；不叫电源正负脚。','有环K接LOCK+；无环A接LOCK−。','接在线圈两端，不串进锁的供电线。'],AMBER)
    s.line([(875,420),(965,420)],GRAY,4);s.rect(965,393,170,54,'#242b32');s.rect(1100,393,15,54,'#dfe9ef')
    s.line([(1135,420),(1450,420)],RED,4);s.text(855,458,'A 无环 → LOCK−',23,GRAY);s.text(1165,458,'K 有环 → LOCK+',23,RED)
    s.box(65,575,700,200,'瓷片100nF与电阻',['100nF常标104，两脚无极性。','输入去耦：一脚12V+，另一脚12V−。','ADC滤波：一脚采样A点，另一脚地B点。','电阻无方向；100k与22k不可装反位置。'],BLUE)
    s.box(815,575,720,200,'一个节点可以拆成多个焊盘',['相邻洞洞板孔默认不导通。','要用绝缘导线或短跳线把同名焊点连起来。','不要四根线硬塞一个杜邦孔或螺丝端子。','功率分支用足额连接器/铜导线，不拖锡作线。'],BLUE)
    s.text(70,820,'示例：ADC节点的四个焊盘，同一条蓝线连通。',25)
    for i,label in enumerate(['100k端','22k端','104一脚','PC0信号线']):
        x=180+i*345;dot(s,x,925,'',BLUE);s.text(x-40,858,label,24)
    s.line([(180,925),(1215,925)],BLUE,5)
    s.text(70,985,'焊完先断电通断：四个蓝点两两相通；与地不应因误焊直接短接（电阻/电容会影响读数）。',22)
    names.append(s.save())

    s=Sheet(13,'ADuM芯片方向与模块追线','芯片顶视图：文字朝自己、缺口在上；左侧1→4，右侧8→5。此图不规定商品模块排针顺序。')
    s.rect(590,190,420,490,'#e6edf3');s.circle(620,220,10,GRAY)
    s.text(685,220,'ADuM1201',30);s.text(710,272,'顶视图',24)
    left=['1 VDD1','2 VOA','3 VIB','4 GND1'];right=['8 VDD2','7 VIA','6 VOB','5 GND2']
    ln=['MCU 3V3','P2-28 / PD6 RX','P2-29 / PD5 TX','MCU GND']
    rn=['RK匹配3.3V电源','RK UART_TX','RK UART_RX','RK GND']
    for i in range(4):
        y=340+i*85;col=RED if i==0 else GRAY if i==3 else BLUE
        s.line([(315,y),(590,y)],col,4);s.line([(1010,y),(1285,y)],col,4)
        s.rect(560,y-15,30,30,GRAY);s.rect(1010,y-15,30,30,GRAY)
        s.text(610,y-20,left[i],24);s.text(830,y-20,right[i],24)
        s.text(70,y-20,ln[i],22,col);s.text(1295,y-20,rn[i],19,col)
        if i in (1,2):s.text(780,y-20,'←' if i==1 else '→',28,BLUE)
    s.box(65,735,710,280,'断电追线后，给模块排针贴标签',['先断开模块两侧全部电源和连接线。','黑笔接某个模块排针，红笔接对应芯片焊盘。','通断核实后标：VDD1 / VOA / VIB / GND1。','细脚不好接触时量模块焊盘，不硬戳相邻脚。','芯片侧电容100nF：1与4之间，8与5之间。','去耦电容只接本侧；GND1和GND2不得并。'],BLUE)
    s.box(815,735,720,280,'测电压与未确认项',['万用表DC V：黑4红1，量MCU侧供电。','换到RK侧：黑5红8，量RK侧供电。','两侧分开测，不用黑4红8判断供电。','RK板型/针号/IO域不明时，右侧暂不接。','1.8V不能直接使用本ADuM模块方案。','单侧掉电时检查反灌和接收端电压，留证据。'],RED)
    names.append(s.save())

    s=Sheet(14,'万用表表笔接法放大','只画直流电压测量；读数为预期示例，仪表量程按自己的万用表说明书选择。')
    for x,title,display,black_label,red_label in [(65,'A 分压节点电压','2.164 V','黑：B / MCU GND','红：A / 两电阻公共点'),(855,'B MOS输出电压','约12 V','黑：MOS OUT−','红：MOS OUT+')]:
        s.text(x,165,title,28);black,red=meter(s,x+70,225,display)
        s.line([black,(black[0],600),(x+100,600)],GRAY,5)
        s.line([red,(red[0],685),(x+610,685)],RED,5)
        dot(s,x+100,600,'',GRAY);dot(s,x+610,685,'',RED)
        s.text(x,555,black_label,23,GRAY);s.text(x+215,715,red_label,22,RED)
    s.box(65,785,710,240,'A：测分压时',['PC0信号线先从MCU拔掉，红笔触节点A。','供电为12.00V时，节点理想约2.164V。','黑笔接分压板地，不能接锁负OUT−。','任何换线先断电；MCU断电时防ADC注入。','测量后按VREF+范围决定能否接回PC0。'],BLUE)
    s.box(815,785,720,240,'B：测MOS时，锁先不接',['OUT+与OUT−之间接10k电阻测试负载。','红OUT+、黑OUT−；不要量到DC−冒充输出。','高有效候选：PWM低→约0V，高→约12V。','黑COM、红VΩ；电流孔在本图中不使用。','电流档绝不跨接电源；功率能力仍需另测。'],RED)
    names.append(s.save())
    return names
