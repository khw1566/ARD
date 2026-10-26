"""Generate the presentation document. No firmware/native build is invoked."""
from pathlib import Path
import hashlib, json, math, html
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.colors import HexColor
from pypdf import PdfReader
import pypdfium2 as pdfium

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[2]
LEGACY = Path('C:/Users/work/Desktop/ETC/ARD_software/results/kicad_legacy_261017')
W,H = 841.89,595.28  # A4 landscape, points
INK='#173247'; MUTED='#567080'; BLUE='#2365A5'; TEAL='#087F83'; ORANGE='#BF5A17'
LIGHT='#F0F5F8'; LINE='#D5E0E7'; WHITE='#FFFFFF'
pdfmetrics.registerFont(TTFont('MG','C:/Windows/Fonts/malgun.ttf'))
pdfmetrics.registerFont(TTFont('MGB','C:/Windows/Fonts/malgunbd.ttf'))
pdf = OUT/'ARD_시스템_회로구성_2장.pdf'
c = canvas.Canvas(str(pdf),pagesize=(W,H))
c.setTitle('ARD 시스템 회로 구성 및 전원 계통')
c.setAuthor('ARD project')
svg=[]; widths=[]

def rect(x,y,w,h,fill=WHITE,stroke=LINE,dash=False,r=6):
    c.setFillColor(HexColor(fill)); c.setStrokeColor(HexColor(stroke));c.setLineWidth(.8)
    c.setDash(4,3) if dash else c.setDash()
    c.roundRect(x,H-y-h,w,h,r,fill=1,stroke=1);c.setDash()
    svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}" stroke-width=".8"'+(' stroke-dasharray="4 3"' if dash else '')+'/>')

def text(x,y,s,size=10,color=INK,bold=False):
    font='MGB' if bold else 'MG'
    c.setFillColor(HexColor(color)); c.setFont(font,size);c.drawString(x,H-y-size,s)
    widths.append((x+pdfmetrics.stringWidth(s,font,size),y+size,s))
    svg.append(f'<text x="{x}" y="{y+size*.88}" font-family="Malgun Gothic, sans-serif" font-size="{size}" font-weight="{700 if bold else 400}" fill="{color}">{html.escape(s)}</text>')

def line(points,color=BLUE,width=1.6,dash=False,arrow=False,both=False):
    c.setStrokeColor(HexColor(color));c.setLineWidth(width)
    c.setDash(4,3) if dash else c.setDash()
    p=c.beginPath();p.moveTo(points[0][0],H-points[0][1])
    for x,y in points[1:]:p.lineTo(x,H-y)
    c.drawPath(p);c.setDash()
    svg.append('<polyline points="'+' '.join(f'{x},{y}' for x,y in points)+f'" fill="none" stroke="{color}" stroke-width="{width}"'+(' stroke-dasharray="4 3"' if dash else '')+'/>')
    if arrow:
        ends=[(points[-2],points[-1])]
        if both:ends.append((points[1],points[0]))
        for (x0,y0),(x,y) in ends:
            a=math.atan2(y-y0,x-x0);d=5
            pts=[(x-d*math.cos(a-.45),y-d*math.sin(a-.45)),(x,y),(x-d*math.cos(a+.45),y-d*math.sin(a+.45))]
            line(pts,color,width)

def card(x,y,w,h,title,rows,accent=TEAL,fill=WHITE,dash=False):
    rect(x,y,w,h,fill,accent,dash)
    text(x+13,y+11,title,12,accent,True)
    for i,s in enumerate(rows):text(x+13,y+35+i*16,s,9.3)

def header(n,title,sub):
    global svg
    svg=[]
    rect(0,0,W,7,TEAL,TEAL,r=0)
    text(32,22,'ARD / SYSTEM ENGINEERING',9,TEAL,True)
    text(32,42,title,23,INK,True)
    text(32,77,sub,9.5,MUTED)
    text(738,25,'2026.10.26',9,MUTED)

def footer(n):
    line([(32,556),(810,556)],LINE,.7)
    text(32,565,'발표용 연결 요약 | 실물 전원 정보 + 소스 기반 신호 | 전기적 정격·실배선 검증 별도',8,MUTED)
    text(751,564,f'{n:02d} / 02',9,TEAL,True)
    (OUT/f'page_{n:02d}.svg').write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}pt" height="{H}pt" viewBox="0 0 {W} {H}"><rect width="100%" height="100%" fill="white"/>'+''.join(svg)+'</svg>',encoding='utf8')
    c.showPage()

header(1,'시스템 구성 및 제어 흐름','JC1060 통합 단말을 추가하고, 기존 구동·촬영·로봇 보드의 역할을 함께 정리했다.')
card(32,112,229,104,'JC1060P470C | 통합 단말',[
    '화면·터치 조작 / 내장 마이크·스피커',
    'ESP32-P4 + ESP32-C6 / ES8311',
    '별도 전원 + 제작사 제공 배터리 모듈'])
card(324,112,213,104,'PC CENTER | ARD_MAIN',[
    '명령 중계 / 상태·영상 통합',
    '선별 시퀀스 / 음성 인식·응답',
    '보드별 네트워크 연결 관리'],BLUE,LIGHT)
line([(261,165),(324,165)],BLUE,1.6,arrow=True,both=True)
text(270,137,'조작·음성',8.3,BLUE)
text(274,180,'상태·영상',8.3,BLUE)
card(600,112,209,104,'기존 단말 | 선택 구성',[
    'ControlMCU: 조이스틱·오디오',
    'HMI_MCU: CYD 화면·터치',
    'JC1060 전환 시 역할 대체 가능'],MUTED,WHITE,True)
line([(600,165),(537,165)],MUTED,1.2,True,True,True)
text(549,137,'설정 선택',8.2,MUTED)
line([(430,216),(430,246)],BLUE,1.6)
line([(146,246),(694,246)],BLUE,1.6)
text(32,229,'Wi-Fi / LAN',9,BLUE,True)
for x in [146,430,694]:line([(x,246),(x,280)],BLUE,1.6,arrow=True,both=True)
card(32,280,229,108,'DriveMCU | 독립 구동 보드',[
    'ESP32-WROOM-32D / A4988 3축',
    '센서·OLED / GPS·부저 인터페이스',
    'PC 서버의 구동 명령 수신'])
card(316,280,229,108,'ImageSenderMCU | 영상',[
    'ESP32-S3 / DVP 병렬 카메라',
    '촬영 이미지 → PC → 화면 표시',
    'Drive 측 5V 전원에서 공급'])
card(580,280,229,108,'Robot3AxisMCU | 로봇',[
    '3축 STEP / DIR / EN',
    '4채널 릴레이 / 공압 그리퍼',
    'L298N DC 모터 제어'],MUTED)
rect(32,412,505,112,LIGHT,LINE)
text(46,423,'JC1060이 추가되어도 구동은 DriveMCU가 담당',12,TEAL,True)
for i,s in enumerate([
    '터치·음성 입력 → PC 서버 → DriveMCU / Robot3AxisMCU',
    '카메라 영상 → PC 서버 → JC1060 화면',
    'JC1060 내장 화면·터치·오디오를 사용하며 외부 모터·센서는 연결하지 않는다.',
    '파란 선: 네트워크 데이터 흐름 / 회색 점선: 기존 단말의 선택 경로']):text(46,449+i*16,s,9.1)
rect(554,412,255,112,WHITE,LINE)
text(568,423,'전원 구성의 핵심 변경',12,ORANGE,True)
for i,s in enumerate([
    '12V → 스텝다운 → 5V',
    'DriveMCU·센서·ImageSenderMCU 공급',
    'JC1060은 별도 전원·배터리 계통',
    '전원 및 주요 GPIO 연결은 다음 쪽 참조']):text(568,449+i*16,s,9.1)
footer(1)

header(2,'전원 계통 및 주요 신호 연결','전원 공급은 사용자 실물 설명을 반영했다. GPIO는 현재 소스와 기존 회로도에서 추출했다.')
rect(32,103,777,213,'#FFFCF8',LINE)
text(45,112,'Drive / Camera 전원 계통',10,ORANGE,True)
rect(45,161,89,56,WHITE,ORANGE);text(58,170,'12V 전원',12,ORANGE,True);text(58,192,'입력 공급',9)
rect(173,151,139,76,WHITE,ORANGE);text(187,163,'DC-DC 스텝다운',11,ORANGE,True);text(187,185,'12V 입력 → 5V 출력',9);text(187,205,'모델·정격: 확인 필요',8,MUTED)
line([(134,189),(173,189)],ORANGE,2,arrow=True)
line([(312,189),(345,189)],ORANGE,2)
line([(345,146),(345,275)],ORANGE,2)
for y,title,sub in [(132,'DriveMCU','보드 5V 전원 입력'),(191,'센서·주변 모듈','5V 공급 / 내부 전압은 모듈별 확인'),(250,'ImageSenderMCU','보드 5V 전원 입력')]:
    line([(345,y+14),(383,y+14)],ORANGE,2,arrow=True)
    rect(383,y,200,42,WHITE,ORANGE)
    text(394,y+3,title,10,ORANGE,True);text(394,y+22,sub,8)
text(320,116,'5V',10,ORANGE,True)
text(600,112,'JC1060 독립 전원 계통',10,TEAL,True)
rect(600,137,193,46,WHITE,TEAL);text(612,142,'별도 외부 전원',10,TEAL,True);text(612,161,'+ 제작사 제공 배터리 모듈',9)
line([(696,183),(696,212)],TEAL,1.8,arrow=True)
rect(600,212,193,44,WHITE,TEAL);text(612,218,'JC1060P470C',12,TEAL,True);text(612,237,'전압·충전/전환 방식: 확인 필요',8,MUTED)
text(600,273,'Drive 측 5V와의 연결은 표시하지 않음',8.4,MUTED)
text(45,299,'선은 공급·귀환을 묶은 전원 경로 요약이다. 5V 공급은 GPIO 신호 전압을 뜻하지 않는다.',8.5,MUTED)

def table(x,y,w,title,rows):
    rect(x,y,w,157,WHITE,LINE)
    rect(x,y,w,26,LIGHT,LINE,r=3)
    text(x+10,y+5,title,10,TEAL,True)
    for i,(a,b) in enumerate(rows):
        yy=y+34+i*18
        text(x+10,yy,a,8.6,MUTED)
        text(x+91,yy,b,8.6)

table(32,334,250,'DriveMCU | 현재 소스',[
    ('축 / STEP·DIR·EN','X: 18 / 19 / 21'),
    ('','Y: 16 / 17 / 5'),
    ('','Z: 25 / 26 / 27'),
    ('I2C SDA / SCL','22 / 23'),
    ('GPS RX / TX','32 / 33 (기본 꺼짐)'),
    ('부저','4')])
table(296,334,250,'ImageSenderMCU | 현재 소스',[
    ('D0 / D1 / D2 / D3','11 / 9 / 8 / 10'),
    ('D4 / D5 / D6 / D7','12 / 18 / 17 / 16'),
    ('XCLK / PCLK','15 / 13'),
    ('VSYNC / HREF','6 / 7'),
    ('SCCB SDA / SCL','4 / 5'),
    ('PWDN / RESET','미사용 (-1)')])
table(560,334,249,'Robot3AxisMCU | 기존 도면',[
    ('축 / STEP·DIR·EN','X: 9 / 10 / 11'),
    ('','Y: 12 / 13 / 14'),
    ('','Z: 8 / 3 / 46'),
    ('릴레이 IN1~4','4 / 5 / 6 / 7'),
    ('L298N IN1~4','15 / 16 / 17 / 18'),
    ('전원·정격','기존 자료상 미확정')])
text(32,502,'센서 버스: AHT20 0x38 / BMP280 0x76·0x77 / SSD1306 0x3C. ToF는 현 설정에서 비활성.',8.6)
text(32,518,'근거: 기존 KiCad·pinmap.csv / DriveMCU board.h·sdkconfig / ImageSenderMCU board_pins.h / JC1060 README·board.h.',8,MUTED)
text(32,533,'확인 필요: 스텝다운 용량, 모듈별 전원·풀업 전압, 모터 전력 배선, 접지·보호회로, JC1060 배터리 모듈 사양.',8,MUTED)
footer(2)
c.save()
reader=PdfReader(str(pdf))
doc=pdfium.PdfDocument(str(pdf))
for i in range(len(doc)):
    doc[i].render(scale=2.2).to_pil().save(OUT/f'page_{i+1:02d}.png')
sources=[LEGACY/'pinmap.csv',LEGACY/'ARD_Legacy.kicad_sch',ROOT/'ARD_ESP-IDF/DriveMCU/main/board.h',ROOT/'ARD_ESP-IDF/DriveMCU/sdkconfig',ROOT/'ARD_ESP-IDF/ImageSenderMCU/main/board_pins.h',ROOT/'ARD_ESP-IDF/JC1060P470C/main/board.h',ROOT/'ARD_ESP-IDF/JC1060P470C/README.md']
checks={'pages':len(reader.pages),'format':'A4 landscape','text_bounds_ok':all(x<812 and y<585 for x,y,s in widths),'source_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},'hardware_evidence':'User supplied power topology; no electrical or device test performed','builds_run':False}
assert checks['pages']==2 and checks['text_bounds_ok'],checks
for term in ['JC1060','12V','5V','배터리','ImageSenderMCU']:
    assert term in ''.join(p.extract_text() for p in reader.pages),term
(OUT/'validation.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'pdf':str(pdf),'pages':checks['pages'],'text_bounds_ok':checks['text_bounds_ok']},ensure_ascii=False))
