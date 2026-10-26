"""Generate a native two-sheet KiCad module interconnection schematic."""
from pathlib import Path
import csv,json,uuid,re,hashlib,math

OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[2]
OLD=Path('C:/Users/work/Desktop/ETC/ARD_software/results/kicad_legacy_261017')
PROJECT='ARD_2Sheet'
def uid(s):return str(uuid.uuid5(uuid.NAMESPACE_URL,'ard-two-sheet-20261026/'+s))
def q(s):return json.dumps(str(s),ensure_ascii=False)
RID=uid('root')
def grid(x):return round(round(x/1.27)*1.27,4)
sources={}
def read(p):
    p=Path(p);b=p.read_bytes();sources[str(p)]=hashlib.sha256(b).hexdigest();return b.decode('utf-8-sig')
rows=list(csv.DictReader(read(OLD/'pinmap.csv').splitlines()))
net_expected={}; unknown=[]

class Sheet:
    def __init__(self,name,num,title):
        self.name=name;self.num=num;self.title=title;self.items=[];self.libs=[];self.id=RID if num==1 else uid(name)
        self.text(title,12,14,2.6)
        self.text('ARD | 2026-10-26 | 핀 이름은 기능/GPIO 식별자이며 물리 커넥터 순번이 아님',12,21,1.25)
        self.text('같은 이름의 라벨은 같은 시트에서 전기적으로 연결됨. 무선 통신은 배선으로 그리지 않음.',12,271,1.2)
        self.text('TBD: 실물 확인 필요. 모듈/보드 단위 회로도이며 IC 내부 회로·전류 용량·PCB 제작 검증을 뜻하지 않음.',12,277,1.2)
    def text(self,s,x,y,size=1.2):
        self.items.append(f'(text {q(s)} (at {x} {y} 0) (effects (font (size {size} {size})) (justify left)) (uuid {q(uid(self.name+str(len(self.items))))}))')
    def wire(self,a,b):
        self.items.append(f'(wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) (stroke (width 0) (type default)) (uuid {q(uid(self.name+"wire"+str(len(self.items))))}))')
    def module(self,ref,value,pins,x,y,w=34):
        # pins: (logical number, visible name, net or None, side L/R)
        x,y,w=grid(x),grid(y),round(round(w/2.54)*2.54,4)
        counts={s:sum(p[3]==s for p in pins) for s in 'LR'}
        h=round(max(10.16,math.ceil((max(counts.values())+1)*3.81/2.54)*2.54),4)
        n=self.name+'_'+ref
        sym=[f'(symbol {q("ARD2:"+n)} (pin_numbers hide) (pin_names (offset .8)) (in_bom yes) (on_board no)',
             f'(property "Reference" {q(ref)} (at 0 {h/2+4} 0) (effects (font (size 1.2 1.2))))',
             f'(property "Value" {q(value)} (at 0 {h/2+1.6} 0) (effects (font (size 1.1 1.1))))',
             f'(symbol {q(n+"_0_1")} (rectangle (start {-w/2} {h/2}) (end {w/2} {-h/2}) (stroke (width .254) (type default)) (fill (type background))))',
             f'(symbol {q(n+"_1_1")}']
        used={'L':0,'R':0};out={}
        for number,name,net,side in pins:
            used[side]+=1;py=round(h/2-used[side]*3.81,4)
            px=round((-w/2-2.54) if side=='L' else (w/2+2.54),4)
            angle=0 if side=='L' else 180
            sym.append(f'(pin passive line (at {px} {py} {angle}) (length 2.54) (name {q(name)} (effects (font (size 1.27 1.27)))) (number {q(number)} (effects (font (size 1 1)))))')
            a=(round(x+px,4),round(y-py,4)); b=(round(a[0]+(-7.62 if side=='L' else 7.62),4),a[1])
            out[str(number)]=(a,b)
            if net:
                self.wire(a,b)
                # Left-side labels extend away from the body, so long TBD names do not cover pin names.
                justify='right bottom' if side=='L' else 'left bottom'
                self.items.append(f'(label {q(net)} (at {b[0]} {b[1]} 0) (effects (font (size 1.27 1.27)) (justify {justify})) (uuid {q(uid(n+str(number)+"lab"))}))')
                net_expected.setdefault(self.name,{}).setdefault(net,[]).append([ref,str(number)])
                if 'TBD' in net:unknown.append({'sheet':self.name,'ref':ref,'port':number,'net':net})
            else:
                self.items.append(f'(no_connect (at {a[0]} {a[1]}) (uuid {q(uid(n+str(number)+"nc"))}))')
        sym.append('))');self.libs.append('\n'.join(sym))
        path='/'+RID+('/'+uid('child') if self.num==2 else '')
        self.items.append(f'''(symbol (lib_id {q('ARD2:'+n)}) (at {x} {y} 0) (unit 1) (in_bom yes) (on_board no) (dnp no) (uuid {q(uid(n+'inst'))})
        (property "Reference" {q(ref)} (at {x} {y-h/2-4} 0) (effects (font (size 1.2 1.2))))
        (property "Value" {q(value)} (at {x} {y-h/2-1.6} 0) (effects (font (size 1.1 1.1))))
        (instances (project {q(PROJECT)} (path {q(path)} (reference {q(ref)}) (unit 1)))))''')
        return out
    def save(self):
        (OUT/(self.name+'.kicad_sch')).write_text(f'''(kicad_sch (version 20250114) (generator "eeschema") (uuid {q(self.id)}) (paper "A3")
        (title_block (title {q(self.title)}) (date "2026-10-26") (rev "2.0") (company "ARD / module interconnection")
          (comment 1 "Logical module ports; TBD = unresolved physical wiring. All pins passive.")
          (comment 2 "Power topology from user. Not a PCB fabrication design."))
        (lib_symbols {' '.join(self.libs)}) {' '.join(self.items)}
        (sheet_instances (path "/" (page {q(self.num)}))) (embedded_fonts no))''',encoding='utf8')

def port(n,net,side='R',name=None):return(str(n),name or str(n),net,side)
def dev_rows(board,device=None):return [r for r in rows if r['board']==board and (device is None or r['device_ref']==device)]
def mcu(p,ref,label,rs,x,y,power=(),w=34):
    gp={r['gpio']:r['net'] for r in rs}
    pins=[port(g,n,name='GPIO'+g) for g,n in sorted(gp.items(),key=lambda a:int(a[0]))]+list(power)
    return p.module(ref,label,pins,x,y,w)
def peripheral(p,ref,label,rs,x,y,extra=(),w=38):
    return p.module(ref,label,[port((r['device_ref']+'_'+r['terminal']) if ref in ['J60','J70'] else r['terminal'],r['net'],'L',r['terminal']) for r in rs]+list(extra),x,y,w)

p=Sheet(PROJECT,1,'01  DriveMCU / ImageSenderMCU / 신호 및 전원')
# Real power connector and step-down with two electrical conductors.
p.text('12V → 5V: 사용자 설명에 따른 실제 전원 공급 계통',12,31,1.35)
j=p.module('J1','12V DC INPUT',[port('+','P12','R','+12V'),port('-','D0V','R','0V')],30,51,25)
b=p.module('U1','STEP-DOWN 12V / 5V',[port('IN+','P12','L'),port('IN-','D0V','L'),port('OUT+','D5V','R','+5V'),port('OUT-','D0V','R','0V')],129,51,42)
for a,z in [('+' ,'IN+'),('-','IN-')]:p.wire(j[a][1],b[z][1])
p.text('D5V / D0V → DriveMCU・센서 모듈・ImageSenderMCU',183,46,1.5)
p.text('스텝다운 모델·정격 / 모터 VMOT·드라이버 VDD는 실물 확인 필요',183,54,1.15)
p.text('XH60 단자는 사용자 요청에 따라 생략. 5V 전원과 GPIO 신호 전압은 별개.',183,61,1.15)
drive=[r for r in dev_rows('DriveMCU') if r['terminal'] not in ['SW1','SW2','POT_WIPER']]
read(ROOT/'ARD_ESP-IDF/DriveMCU/main/board.h')
mcu(p,'U10','DriveMCU / ESP32',drive,43,143,[port('5V','D5V'),port('GND','D0V')])
for i,axis in enumerate('XYZ'):
    rs=dev_rows('DriveMCU',f'U30{i+1}')
    peripheral(p,f'U1{i+1}',f'A4988 {axis}',rs,141,95+i*44,[port('VMOT',f'{axis}_VMOT_TBD','L'),port('VDD',f'{axis}_VDD_TBD','L'),port('GND',f'{axis}_GND_TBD','L'),port('COILS',f'{axis}_COILS_TBD','L')],36)
for i,di in enumerate(['U401','U402','U403','U404','U405']):
    label={'U401':'SSD1306 0x3C','U402':'AHT20 0x38','U403':'BMP280 0x76/77','U404':'ToF / OFF','U405':'NEO-6M / OFF'}[di]
    peripheral(p,f'U2{i+1}',label,dev_rows('DriveMCU',di),232,88+i*30,[port('VCC','D5V','L'),port('GND','D0V','L')],38)
peripheral(p,'BZ1','BUZZER', [r for r in drive if r['terminal']=='BUZZER_DRIVE'],141,227,[port('RETURN','BUZZ_RET_TBD','L')],36)
cam=dev_rows('ImageSenderMCU');read(ROOT/'ARD_ESP-IDF/ImageSenderMCU/main/board_pins.h')
mcu(p,'U30','ImageSender / ESP32-S3',cam,340,116,[port('5V','D5V'),port('GND','D0V')],43)
peripheral(p,'U31','DVP CAMERA',cam,340,213,[port('PWR','CAM_PWR_TBD','L'),port('GND','CAM_GND_TBD','L'),port('PWDN',None,'L'),port('RESET',None,'L')],43)
p.text('카메라 PWDN / RESET: GPIO 제어 미사용(-1).',286,253,1.15)
p.text('GPS·ToF 기본 꺼짐. 센서 VCC는 모듈 입력 기준 5V 공급 설명 반영.',12,250,1.15)
p.text('센서 내부 레귤레이터·I2C 풀업 전압 / 모터 코일·보호회로는 TBD.',12,256,1.15)

s=Sheet('02_Terminals_Robot',2,'02  JC1060・Robot3Axis・기존 선택 단말 / 신호·독립 전원')
read(ROOT/'ARD_ESP-IDF/JC1060P470C/main/board.h')
cfg=read(ROOT/'ARD_ESP-IDF/JC1060P470C/sdkconfig')
ext=s.module('J2','JC EXTERNAL POWER',[port('+','JC_EXT_P_TBD'),port('-','JC_EXT_N_TBD')],34,47,39)
bat=s.module('BT1','MAKER BATTERY MODULE',[port('+','JC_BAT_P_TBD'),port('-','JC_BAT_N_TBD')],137,47,49)
s.text('JC1060: 별도 전원 + 제작사 배터리 모듈. 전압·충전/전환·실제 단자 순서 TBD.',12,64,1.1)
jcpins=[port('EXT+','JC_EXT_P_TBD','L'),port('EXT-','JC_EXT_N_TBD','L'),port('BAT+','JC_BAT_P_TBD','L'),port('BAT-','JC_BAT_N_TBD','L')]
jcmap={7:'JC_SDA',8:'JC_SCL',0:'JC_LCD_RST',23:'JC_BL',11:'JC_AMP_EN',18:'JC_CLK',19:'JC_CMD',14:'JC_D0',15:'JC_D1',16:'JC_D2',17:'JC_D3',54:'JC_C6_RST'}
for key,net in [('MCLK','JC_MCLK'),('BCLK','JC_BCLK'),('WS','JC_WS'),('DOUT','JC_DOUT'),('DIN','JC_DIN')]:
    g=int(re.search(r'^CONFIG_ARD_VOICE_'+key+r'=(\d+)',cfg,re.M)[1]);jcmap[g]=net
jcpins += [port(g,n,'R','GPIO'+str(g)) for g,n in sorted(jcmap.items())]+[port('DSI','JC_DSI','R','MIPI-DSI')]
s.module('U40','JC1060 / ESP32-P4',jcpins,60,124,42)
s.module('DS1','JD9165 / MIPI-DSI',[port('RST','JC_LCD_RST','L'),port('BL','JC_BL','L'),port('DSI','JC_DSI','L')],150,88,40)
s.module('U41','GT911 / TOUCH',[port('SDA','JC_SDA','L'),port('SCL','JC_SCL','L')],150,120,40)
s.module('U42','ESP32-C6 / SDIO',[port(k,'JC_'+k,'L') for k in ['CLK','CMD','D0','D1','D2','D3']]+[port('RST','JC_C6_RST','L')],150,161,40)
s.module('U43','ES8311 + AMP / AUDIO',[port(k,'JC_'+k,'L') for k in ['SDA','SCL','MCLK','BCLK','WS']]+[port('DIN','JC_DOUT','L'),port('DOUT','JC_DIN','L'),port('AMP_EN','JC_AMP_EN','L')],235,124,43)
s.text('JC 구성요소는 보드 내장 회로. 위 라벨은 내부 인터페이스이며 외부 점퍼 배선 지시가 아님.',12,185,1.05)
s.text('내장 마이크·스피커는 ES8311 오디오 경로 사용. 세부 아날로그 배선·증폭 회로는 보드 내부.',12,190,1.05)
robot=dev_rows('Robot3AxisMCU')
mcu(s,'U50','Robot3Axis / ESP32-S3',robot,286,105,(),39)
for i,axis in enumerate('XYZ'):
    peripheral(s,f'U5{i+1}',f'{axis} STEP DRIVER',dev_rows('Robot3AxisMCU',f'U50{i+1}'),375,49+i*35,[port('PWR',f'R_{axis}_PWR_TBD','L'),port('COILS',f'R_{axis}_COIL_TBD','L')],40)
peripheral(s,'K1','4CH RELAY / LOW',dev_rows('Robot3AxisMCU','K501'),375,166,[port('PWR','R_RELAY_PWR_TBD','L'),port('CONTACTS','R_CONTACT_TBD','L')],40)
peripheral(s,'U55','L298N / DC MOTOR',dev_rows('Robot3AxisMCU','U504'),375,218,[port('PWR','R_DC_PWR_TBD','L'),port('EN','R_DC_EN_TBD','L'),port('OUT','R_DC_OUT_TBD','L')],40)
s.text('CH1/2: 공압 그리퍼',339,186,1.1)
s.text('기존 ControlMCU / HMI_MCU 선택 구성 (JC1060으로 역할 전환 가능)',12,198,1.3)
for board,ref,label,x1,x2,w in [('ControlMCU','U60','Control / ESP32-S3',35,111,29),('HMI_MCU','U70','HMI / CYD ESP32',198,273,32)]:
    rs=dev_rows(board)
    mcu(s,ref,label,rs,x1,234,(),w)
    peripheral(s,'J60' if board=='ControlMCU' else 'J70','CONTROL ASSEMBLY' if board=='ControlMCU' else 'LCD + TOUCH ASSEMBLY',rs,x2,234,(),w+6)
s.text('CONTROL ASSEMBLY: 조이스틱·토글·버튼·LED·MAX98357A / LCD+TOUCH: ILI9341·XPT2046',12,264,1.05)
s.text('Control GPIO35/36은 N16R8 Octal PSRAM과 충돌 가능. 기존 배정 보존 / 전원 미확정.',12,268,1.05)

p.items.append(f'''(sheet (at 12.7 229.87) (size 78.74 7.62) (stroke (width .254) (type default)) (fill (color 0 0 0 0)) (uuid {q(uid('child'))})
 (property "Sheetname" "02 JC1060 / Robot / Legacy" (at 13.97 228.6 0) (effects (font (size 1.2 1.2)) (justify left bottom)))
 (property "Sheetfile" "02_Terminals_Robot.kicad_sch" (at 13.97 238.76 0) (effects (font (size 1 1)) (justify left top)))
 (instances (project {q(PROJECT)} (path {q('/'+RID)} (page "2")))))''')
for page in [p,s]:page.save()
libs=[lib.replace('(symbol "ARD2:','(symbol "',1) for page in [p,s] for lib in page.libs]
(OUT/'ARD2.kicad_sym').write_text('(kicad_symbol_lib (version 20241209) (generator "kicad_symbol_editor") '+'\n'.join(libs)+')',encoding='utf8')
(OUT/'sym-lib-table').write_text('(sym_lib_table (version 7) (lib (name "ARD2") (type "KiCad") (uri "${KIPRJMOD}/ARD2.kicad_sym") (options "") (descr "ARD logical board/module ports")))',encoding='utf8')
(OUT/(PROJECT+'.kicad_pro')).write_text(json.dumps({'meta':{'filename':PROJECT+'.kicad_pro','version':1}},indent=2),encoding='utf8')
(OUT/'expected_nets.json').write_text(json.dumps(net_expected,indent=2),encoding='utf8')
(OUT/'source_manifest.json').write_text(json.dumps({'source_sha256':sources,'unresolved_ports':unknown,'sheets':2,'scope':'Native KiCad module interconnect; user-described supplies; all module pins passive; no fabrication claim'},ensure_ascii=False,indent=2),encoding='utf8')
print('Generated native KiCad project / 2 A3 sheets')
