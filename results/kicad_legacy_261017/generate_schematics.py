"""Reproducible KiCad module-level schematics from the legacy firmware.

Pins on custom module symbols are signal identifiers, NOT header pad numbers.
Only firmware signal nets are connected. Supply/actuator terminals marked TBD
must be resolved against physical modules before this can become a build design.
"""
from pathlib import Path
import csv
import hashlib
import json
import re
import uuid

OUT = Path(__file__).resolve().parent
SRC = OUT.parents[1]
ROOT_ID = str(uuid.uuid5(uuid.NAMESPACE_URL, 'ard-legacy-261017'))
rows, sources = [], {}

def uid(key):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, 'ard-legacy-261017/' + key))

def q(s):
    return json.dumps(str(s), ensure_ascii=False)

def source(path):
    p = SRC / path
    data = p.read_bytes()
    sources[path] = {'sha256': hashlib.sha256(data).hexdigest()}
    return data.decode('utf-8-sig')

def macro(board, name):
    path = f'{board}/src/main.cpp'
    text = source(path)
    found = re.search(r'^\s*#define\s+' + re.escape(name) + r'\s+(-?\d+)\b', text, re.M)
    if not found:
        raise ValueError((path, name))
    return int(found[1]), f'{path}:{text[:found.start()].count(chr(10)) + 1}'

def pin(board, name, terminal, note=''):
    gpio, evidence = macro(board, name)
    prefix = {'ControlMCU':'C','DriveMCU':'D','Robot3AxisMCU':'R','ImageSenderMCU':'CAM','HMI_MCU':'H'}[board]
    return {'gpio': gpio, 'terminal': terminal, 'net': f'{prefix}_G{gpio}', 'source': evidence, 'note': note}

def literal(board, gpio, terminal, path, pattern, note=''):
    text = source(path)
    match = re.search(pattern, text, re.M)
    if not match:
        raise ValueError(pattern)
    prefix = {'Robot3AxisMCU':'R','HMI_MCU':'H'}[board]
    return {'gpio': gpio, 'terminal': terminal, 'net': f'{prefix}_G{gpio}',
            'source': f'{path}:{text[:match.start()].count(chr(10))+1}', 'note': note}

class Page:
    def __init__(self, name, number, title, subtitle, notes):
        self.name, self.number, self.title = name, number, title
        self.subtitle, self.notes = subtitle, notes
        self.id = ROOT_ID if number == 1 else uid(name)
        self.libs, self.items, self.modules, self.nets = [], [], [], {}
        self.text(title, 18, 18, 3.5)
        self.text(subtitle, 18, 27, 1.65)
        self.text('ARD / LEGACY FIRMWARE  |  2026.10.17  |  MODULE INTERCONNECT', 18, 10, 1.2)
        self.text('읽는 법: 같은 이름의 신호 라벨은 전기적으로 연결됨. 핀 번호는 GPIO/신호명이며 커넥터 순번이 아님.', 18, 247, 1.35)
        for i, line in enumerate(notes):
            self.text(line, 18, 255 + i*5, 1.45)

    def text(self, s, x, y, size=1.5):
        self.items.append(f'(text {q(s)} (at {x} {y} 0) (effects (font (size {size} {size})) (justify left)) (uuid {q(uid(self.name+"text"+str(len(self.items))))}))')

    def line(self, x1, y1, x2, y2):
        self.items.append(f'(polyline (pts (xy {x1} {y1}) (xy {x2} {y2})) (stroke (width 0.3) (type default)) (uuid {q(uid(self.name+"line"+str(len(self.items))))}))')

    def module(self, ref, value, pins, x, y, right=False, width=50):
        # All logical ports are passive: this intentionally does not claim bare-IC ERC coverage.
        x, y = (round(v/1.27)*1.27 for v in (x,y))
        width = round(width/2.54)*2.54
        name = self.name + '_' + ref
        height = max(15.24, (len(pins)+1)*5.08)
        self.modules.append((ref, value))
        lib = [f'(symbol {q("ARD:"+name)} {"(pin_numbers hide)" if right else ""} (pin_names (offset 1)) (in_bom yes) (on_board no)',
               f'(property "Reference" {q(ref)} (at 0 {height/2+6} 0) (effects (font (size 1.5 1.5))))',
               f'(property "Value" {q(value)} (at 0 {height/2+2.5} 0) (effects (font (size 1.5 1.5))))',
               f'(symbol {q(name+"_0_1")} (rectangle (start {-width/2} {height/2}) (end {width/2} {-height/2}) (stroke (width 0.3) (type default)) (fill (type background))))',
               f'(symbol {q(name+"_1_1")}']
        for i, (number, pname, net) in enumerate(pins):
            py = round((height/2 - (i+1)*5.08),4)
            px = round((-width/2 - 5.08 if right else width/2 + 5.08),4)
            angle = 0 if right else 180
            lib.append(f'(pin passive line (at {px} {py} {angle}) (length 5.08) (name {q(pname)} (effects (font (size 1.6 1.6)))) (number {q(number)} (effects (font (size 1.2 1.2)))))')
            ax, ay = round(x+px,4), round(y-py,4)
            bx = round(ax-10.16 if right else ax+10.16,4)
            if net:
                self.items.append(f'(wire (pts (xy {ax} {ay}) (xy {bx} {ay})) (stroke (width 0) (type default)) (uuid {q(uid(name+str(i)+"wire"))}))')
                # MCU labels right aligned at the far end, peripheral labels left aligned.
                la = 180 if right else 0
                self.items.append(f'(label {q(net)} (at {bx} {ay} {la}) (effects (font (size 1.6 1.6)) (justify left bottom)) (uuid {q(uid(name+str(i)+"label"))}))')
                self.nets.setdefault(net, []).append((ref, str(number)))
            else:
                self.items.append(f'(no_connect (at {ax} {ay}) (uuid {q(uid(name+str(i)+"nc"))}))')
        lib.append('))')
        self.libs.append('\n'.join(lib))
        parentpath = '/' + ROOT_ID + (('/'+uid('sheet/'+self.name)) if self.number != 1 else '')
        self.items.append(f'''(symbol (lib_id {q('ARD:'+name)}) (at {x} {y} 0) (unit 1)
          (in_bom yes) (on_board no) (dnp no) (uuid {q(uid(name+'instance'))})
          (property "Reference" {q(ref)} (at {x} {y-height/2-6} 0) (effects (font (size 1.6 1.6))))
          (property "Value" {q(value)} (at {x} {y-height/2-2.5} 0) (effects (font (size 1.5 1.5))))
          (instances (project "ARD_Legacy" (path {q(parentpath)} (reference {q(ref)}) (unit 1)))))''')

    def mcu_and_devices(self, board, mcu, devices, refbase):
        gpios = {}
        for _, _, _, mappings, _ in devices:
            for p in mappings:
                gpios[p['gpio']] = p
        ports = [(str(g), f'GPIO{g}', p['net']) for g,p in sorted(gpios.items())]
        self.module(f'U{refbase}', mcu, ports, 60, 140, width=55)
        for index, (ref, value, pos, mappings, extra) in enumerate(devices):
            ports = [(p['terminal'], p['terminal'], p['net']) for p in mappings] + [(p,p,None) for p in extra]
            self.module(ref, value, ports, *pos, right=True, width=58)
            for p in mappings:
                rows.append({'sheet': self.name, 'board': board, 'mcu_ref': f'U{refbase}', 'gpio':p['gpio'],
                             'device_ref':ref, 'device':value, 'terminal':p['terminal'], 'net':p['net'],
                             'source':p['source'], 'status':'CODE_CONFIRMED', 'note':p['note']})

    def save(self):
        body = f'''(kicad_sch (version 20250114) (generator "eeschema")
        (uuid {q(self.id)}) (paper "A3")
        (title_block (title {q(self.title)}) (date "2026-10-17") (rev "1.0 - firmware reconstruction")
          (company "ARD / Presentation") (comment 1 "Signal-level module schematic. Not a fabrication design.")
          (comment 2 "TBD/NC = outside firmware evidence; not confirmed physical no-connect."))
        (lib_symbols {' '.join(self.libs)})
        {' '.join(self.items)}
        (sheet_instances (path "/" (page {q(self.number)})))
        (embedded_fonts no))'''
        (OUT / (self.name+'.kicad_sch')).write_text(body, encoding='utf-8')

PAGES=[]
def page(name,title,subtitle,notes):
    p=Page(name,len(PAGES)+1,title,subtitle,notes)
    PAGES.append(p)
    return p

overview=page('ARD_Legacy','ARD 구형 시스템 / 전체 구성',
    '5개 MCU의 역할과 무선 통신 구조 · 세부 GPIO 연결은 다음 시트 참조',[
    '범위: ControlMCU / DriveMCU / Robot3AxisMCU / ImageSenderMCU / HMI_MCU의 현재 소스.',
    'ImageSenderMCU_P4는 S3 카메라의 대체 보드이므로 본체 회로에서 제외. SoundMCU는 현재 폴더에 소스 없음.',
    '전원 장치, 배선 색상, 커넥터 순서, 보호회로와 부품 정격은 미확정. 실물 배선 검증 전 제작용으로 사용하지 말 것.'
])
overview.text('PC CENTER',180,60,3)
overview.text('AI 분석 / 대시보드 / 제어',163,68,1.7)
overview.text('Wi-Fi AP / LAN',174,93,2.3)
overview.text('HTTP 5000 · 영상 UDP 5001 · 오디오 UDP 5005 · 명령 TCP 5002',103,105,1.6)
overview.line(206,73,206,88)
names=[('ControlMCU','조이스틱 / 음성 출력'),('DriveMCU','3축 구동 / GPS / 센서'),('Robot3AxisMCU','3축 로봇 / 릴레이 / DC'),('ImageSenderMCU','병렬 카메라 / 영상 전송'),('HMI_MCU','LCD / 터치 / UI')]
for i,(name,desc) in enumerate(names):
    x=18+i*79
    overview.line(x+34,120,x+34,137)
    for a,b,c,d in [(x,137,x+69,137),(x,137,x,162),(x+69,137,x+69,162),(x,162,x+69,162)]:overview.line(a,b,c,d)
    overview.text(name,x+3,146,1.9)
    overview.text(desc,x+3,154,1.35)
overview.line(52,120,368,120)
overview.line(206,110,206,120)
overview.text('위 선은 네트워크 흐름도이며 전선 / 신호 넷이 아님',135,178,1.6)
overview.text('도면 탐색',18,193,2.3)

p=page('02_Control','ControlMCU / 조작 및 오디오', 'ESP32-S3 · 디지털 조이스틱 / 모드 토글 / MAX98357A · 입력은 내부 풀업',[
    '입력 스위치는 코드상 INPUT_PULLUP: 스위치 ON 시 GPIO-GND 연결로 해석. 기구 배선 / 토글 COM은 실물 확인.',
    'GPIO35/36: 주석의 N16R8 + Octal PSRAM 보드라면 메모리 신호와 충돌 가능. 현재 배정을 기록했으며 수정하지 않음.',
    'LED 직렬 저항 / 오디오 전원 / 스피커 정격 / 외부 LED 구동부는 소스만으로 확인 불가. 전원 핀은 TBD.'
])
b='ControlMCU'
devices=[('SW201','4-way joystick / active LOW',(225,75),[pin(b,'JOY_'+a,a) for a in ['UP','DOWN','LEFT','RIGHT']],['COM_GND_TBD']),
 ('SW202','Mode toggle A / B',(350,75),[pin(b,'TOGGLE_'+a,a,'PSRAM pin conflict conditional on actual N16R8 board') for a in ['A','B']],['COM_GND_TBD']),
 ('SW203','Push button / active LOW',(225,126),[pin(b,'PUSH_BTN','CONTACT')],['COM_GND_TBD']),
 ('D201','Button indicator LED',(350,126),[pin(b,'BUTTON_LED_PIN','DRIVE')],['RETURN_TBD']),
 ('U201','MAX98357A I2S amplifier',(225,191),[pin(b,a,t) for a,t in [('I2S_DOUT','DIN'),('I2S_WS','LRC'),('I2S_BCLK','BCLK'),('AMP_SD','SD')]],['VIN_TBD','GND_TBD','SPK+_TBD','SPK-_TBD']),
 ('D202','WS2812B / onboard',(350,192),[pin(b,'LED_PIN','DIN')],['VDD_ONBOARD','GND_ONBOARD'])]
p.mcu_and_devices(b,'ESP32-S3 / board ports',devices,200)

p=page('03_Drive_Motors','DriveMCU / 3축 스텝 모터','ESP32-WROOM32D · A4988 X/Y/Z · ENABLE active LOW · GPIO는 소스에서 추출',[
    'A4988의 VMOT / VDD / GND / RESET / SLEEP / MS1~3 / 코일 배선 및 전류 제한은 펌웨어에서 확인할 수 없음.',
    'TBD 핀의 X 표시는 증거 범위 밖이라는 뜻. 실제 회로에서 미연결로 두어도 된다는 뜻이 아님.',
    'X/Y는 차동 구동, Z는 레일. 드라이버 전원과 모터 정격을 확정한 후 전원 / 보호 / 디커플링 회로 설계 필요.'
])
b='DriveMCU'
devices=[]
for i,axis in enumerate('XYZ'):
    devices.append((f'U30{i+1}',f'A4988 / {axis} axis',(270,78+i*62),[pin(b,a+'_PIN_'+axis,t) for a,t in [('STEP','STEP'),('DIR','DIR'),('EN','ENABLE_N')]],['VMOT_TBD','VDD_TBD','GND_TBD','CONFIG_TBD','COILS_TBD']))
p.mcu_and_devices(b,'ESP32-WROOM32D / GPIO',devices,300)

p=page('04_Drive_Sensors','DriveMCU / 센서 및 수동 조작','공유 I2C: SDA GPIO22 / SCL GPIO23 · GPS: UART2 9600 8N1',[
    'I2C 주소: SSD1306 0x3C, AHT20 0x38, BMP280 0x76 → 0x77 순서 탐색, VL53L0X 라이브러리 기본 주소.',
    'VL53L0X는 ARD_ENABLE_TOF=0으로 기본 비활성. GPS 파싱도 enableGPS=false가 기본이며 명령으로 활성화.',
    '버튼 GND, 가변저항 3.3V/GND, 버저 구동 방식, I2C 풀업 및 각 모듈 전원 전압은 실물 확인 필요.'
])
devices=[]
for i,(value,pos) in enumerate([('SSD1306 / 128 x 64 / 0x3C',(220,69)),('AHT20 / 0x38',(350,69)),('BMP280 / 0x76 or 0x77',(220,123)),('VL53L0X / optional, OFF',(350,123))]):
    devices.append((f'U40{i+1}',value,pos,[pin(b,'I2C_SDA','SDA'),pin(b,'I2C_SCL','SCL')],['VCC_TBD','GND_TBD']))
devices += [('U405','NEO-6M / UART2 9600',(220,182),[pin(b,'GPS_RX_PIN','TX','GPS TX -> MCU RX'),pin(b,'GPS_TX_PIN','RX','MCU TX -> GPS RX')],['VCC_TBD','GND_TBD']),
 ('J401','Manual inputs / buzzer',(350,192),[pin(b,a,t) for a,t in [('SW1_PIN','SW1'),('SW2_PIN','SW2'),('POT_PIN','POT_WIPER'),('BUZZER_PIN','BUZZER_DRIVE')]],['COM_TBD','POT_ENDS_TBD','BUZZER_RETURN_TBD'])]
p.mcu_and_devices(b,'ESP32-WROOM32D / GPIO',devices,400)

p=page('05_Robot3Axis','Robot3AxisMCU / 로봇 및 공압 제어','ESP32-S3 N16R8 · STEP/DIR/EN 3축 · 4ch relay / L298N inputs',[
    '3축 스텝 드라이버는 STEP/DIR/EN 인터페이스만 확인됨. A4988 등 정확한 모듈 형식은 미확정.',
    '릴레이는 active LOW, CH1/2는 주석상 공압 그리퍼. COM/NO/NC / 솔레노이드 전압 / 플라이백 회로 확인 필요.',
    'GPIO3/46은 부트 스트랩 핀. L298N ENA/ENB와 모터 / 전원 연결, 드라이버 전류 제한은 코드에 없음.'
])
b='Robot3AxisMCU'
devices=[]
for i,axis in enumerate('XYZ'):
    devices.append((f'U50{i+1}',f'{axis} stepper driver / type TBD',(220,69+i*59),[pin(b,axis+'_'+a+'_PIN',t) for a,t in [('STEP','STEP'),('DIR','DIR'),('EN','ENABLE_N')]],['POWER_TBD','CONFIG_TBD','COILS_TBD']))
path=b+'/src/main.cpp'
devices += [('K501','4ch relay / active LOW',(350,88),[literal(b,g,f'IN{i+1}',path,r'^const int RELAY_PINS', 'CH1/2 pneumatic gripper' if i<2 else '') for i,g in enumerate([4,5,6,7])],['COIL_SUPPLY_TBD','GND_TBD','CONTACTS_TBD']),
 ('U504','L298N / DC motor driver',(350,180),[literal(b,g,f'IN{i+1}',path,r'^const int L298N_PINS') for i,g in enumerate([15,16,17,18])],['ENA_ENB_TBD','POWER_TBD','GND_TBD','MOTOR_OUT_TBD'])]
p.mcu_and_devices(b,'ESP32-S3 N16R8 / GPIO',devices,500)

p=page('06_ImageSender','ImageSenderMCU / 병렬 카메라','ESP32-S3 N16R8 · esp_camera DVP 8-bit · 데이터 순서는 실제 config.pin_d0~d7 기준',[
    '실제 D0~D7 = GPIO11, 9, 8, 10, 12, 18, 17, 16. 기존 요약 문서의 배열과 다르므로 현재 소스를 우선.',
    'PWDN / RESET = -1: MCU가 제어하지 않음. 센서 모듈의 전원 / 리셋 / 클럭 회로와 커넥터 순서는 확인 필요.',
    '문서에는 OV5660 / OV5640가 혼재. 정확한 센서 SKU는 실물 확인. SD 카드 연결은 현재 main.cpp에 없어 제외.'
])
b='ImageSenderMCU'
mapping=[pin(b,f'Y{i+2}_GPIO_NUM',f'D{i}') for i in range(8)]
mapping += [pin(b,a+'_GPIO_NUM',t) for a,t in [('XCLK','XCLK'),('PCLK','PCLK'),('VSYNC','VSYNC'),('HREF','HREF'),('SIOD','SCCB_SDA'),('SIOC','SCCB_SCL')]]
p.mcu_and_devices(b,'ESP32-S3 N16R8 / GPIO',[
 ('U601','DVP camera / exact SKU TBD',(280,139),mapping,['POWER_TBD','GND_TBD','PWDN_NOT_DRIVEN','RESET_NOT_DRIVEN'])],600)

p=page('07_HMI','HMI_MCU / CYD LCD 및 터치','ESP32-2432S028 · ILI9341 LCD + XPT2046 touch · 보드 내부 신호의 기능별 표현',[
    'LCD는 TFT_eSPI 빌드 플래그, 터치는 실제 사용 중인 CYD28_TouchscreenR.h의 핀 정의에서 추출.',
    '터치 SPI는 LCD SPI와 별도: MOSI32 / MISO39 / CLK25 / CS33 / IRQ36. 외부 점퍼 배선 지시가 아님.',
    'LCD RST=-1로 MCU 제어 없음. BL21은 백라이트 제어 신호이며 LED 전류를 GPIO가 직접 공급한다는 뜻이 아님.'
])
b='HMI_MCU'
ini=b+'/platformio.ini'
text=source(ini)
lcd=[]
for key,terminal in [('MOSI','MOSI'),('MISO','MISO'),('SCLK','SCLK'),('CS','CS'),('DC','DC'),('BL','BL_CTRL')]:
    m=re.search(r'-D TFT_'+key+r'=(\d+)',text)
    lcd.append(literal(b,int(m[1]),terminal,ini,r'^\s*-D TFT_'+key+r'=', 'Onboard LCD signal'))
touch=[]
path=b+'/src/CYD28_TouchscreenR.h'
text=source(path)
for key in ['MOSI','MISO','CLK','CS','IRQ']:
    m=re.search(r'#define CYD28_TouchR_'+key+r'\s+(\d+)',text)
    touch.append(literal(b,int(m[1]),key,path,r'^#define CYD28_TouchR_'+key+r'\s', 'Onboard touch signal'))
p.mcu_and_devices(b,'ESP32 / CYD onboard GPIO',[
 ('U701','ILI9341 / onboard LCD',(270,99),lcd,['POWER_ONBOARD','GND_ONBOARD','RST_NOT_DRIVEN']),
 ('U702','XPT2046 / onboard touch',(270,190),touch,['POWER_ONBOARD','GND_ONBOARD','TOUCH_PANEL_ONBOARD'])],700)

# Clickable native hierarchical sheet tiles beneath the overview diagram.
for i,p in enumerate(PAGES[1:]):
    col, row = i%3, i//3
    x,y=18+col*130,202+row*20
    overview.items.append(f'''(sheet (at {x} {y}) (size 117 13) (stroke (width 0.25) (type default))
      (fill (color 0 0 0 0)) (uuid {q(uid('sheet/'+p.name))})
      (property "Sheetname" {q(p.title)} (at {x+2} {y+3} 0) (effects (font (size 1.4 1.4)) (justify left)))
      (property "Sheetfile" {q(p.name+'.kicad_sch')} (at {x+2} {y+10} 0) (effects (font (size 1.2 1.2)) (justify left)))
      (instances (project "ARD_Legacy" (path {q('/'+ROOT_ID)} (page {q(p.number)})))))''')

for p in PAGES:
    p.save()
library = []
for p in PAGES:
    for lib in p.libs:
        library.append(lib.replace('(symbol "ARD:', '(symbol "', 1))
(OUT/'ARD.kicad_sym').write_text('(kicad_symbol_lib (version 20241209) (generator "kicad_symbol_editor")\n'+'\n'.join(library)+'\n)',encoding='utf-8')
(OUT/'sym-lib-table').write_text('(sym_lib_table (version 7) (lib (name "ARD") (type "KiCad") (uri "${KIPRJMOD}/ARD.kicad_sym") (options "") (descr "Firmware-derived logical module ports; not physical footprints")))',encoding='utf-8')
with (OUT/'pinmap.csv').open('w',encoding='utf-8-sig',newline='') as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0]))
    w.writeheader();w.writerows(rows)
source('ControlMCU/platformio.ini')
source('DriveMCU/platformio.ini')
source('Robot3AxisMCU/platformio.ini')
source('ImageSenderMCU/platformio.ini')
source('DriveMCU/include/BoundedAht20.h')
manifest={'date':'2026-10-17','author':'Codex','source_root':str(SRC),'sources':sources,
          'signal_connections':len(rows),'sheets':[{'name':p.name,'number':p.number,'title':p.title,'subtitle':p.subtitle,'notes':p.notes,'nets':p.nets,'modules':p.modules} for p in PAGES],
          'scope':'Firmware-confirmed logical module ports. All supplies, grounds and load terminals unresolved; no fabrication claim.'}
(OUT/'source_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'ARD_Legacy.kicad_pro').write_text(json.dumps({'meta':{'filename':'ARD_Legacy.kicad_pro','version':1}},indent=2),encoding='utf-8')
print(f'Generated {len(PAGES)} sheets / {len(rows)} signal connections in {OUT}')
