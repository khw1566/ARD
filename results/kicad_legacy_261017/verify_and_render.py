"""Check exported KiCad nets against source-derived port pairs; render PDF."""
from pathlib import Path
import csv
import hashlib
import json
import re
import xml.etree.ElementTree as ET

OUT = Path(__file__).resolve().parent
SRC = OUT.parents[1]
manifest = json.loads((OUT/'source_manifest.json').read_text(encoding='utf-8'))
for name,meta in manifest['sources'].items():
    assert hashlib.sha256((SRC/name).read_bytes()).hexdigest() == meta['sha256'], name
robot = (SRC/'Robot3AxisMCU/src/main.cpp').read_text(encoding='utf-8-sig')
for name, expected in [('RELAY_PINS',[4,5,6,7]),('L298N_PINS',[15,16,17,18])]:
    found = re.search(name+r'\[4\]\s*=\s*\{([^}]+)\}',robot)
    assert [int(x.strip()) for x in found[1].split(',')] == expected

tree = ET.parse(OUT/'ARD_Legacy.net.xml')
net_nodes = {}
unresolved = {}
for net in tree.findall('.//nets/net'):
    name = net.attrib['name']
    nodes = {(n.attrib['ref'],n.attrib['pin']) for n in net.findall('node')}
    if name.startswith('unconnected-'):
        assert len(nodes)==1, (name,nodes)
        unresolved[name]=nodes
    else:
        net_nodes[name]=nodes
pinmap = list(csv.DictReader((OUT/'pinmap.csv').open(encoding='utf-8-sig')))
for row in pinmap:
    matching = [nodes for name,nodes in net_nodes.items() if name.endswith('/'+row['net'])]
    assert len(matching)==1, (row['net'],matching)
    assert (row['mcu_ref'],row['gpio']) in matching[0], row
    assert (row['device_ref'],row['terminal']) in matching[0], row

# Verify that no unintended short merged any MCU GPIOs and no extra net endpoints exist.
expected = {}
for row in pinmap:
    expected.setdefault(row['net'],set()).update([(row['mcu_ref'],row['gpio']), (row['device_ref'],row['terminal'])])
assert len(net_nodes)==len(expected), (len(net_nodes), len(expected))
for name,nodes in net_nodes.items():
    assert nodes == expected[name.rsplit('/',1)[-1]], (name,nodes)
erc = (OUT/'erc.rpt').read_text(encoding='utf-8-sig')
assert 'ERC messages: 0  Errors 0  Warnings 0' in erc

from pypdf import PdfReader
from PIL import Image, ImageOps, ImageDraw, ImageFont
import pypdfium2 as pdfium
reader=PdfReader(OUT/'ARD_Legacy.pdf')
assert len(reader.pages)==7
pdf=pdfium.PdfDocument(str(OUT/'ARD_Legacy.pdf'))
png_dir=OUT/'png';png_dir.mkdir(exist_ok=True)
slide_dir=OUT/'presentation_png';slide_dir.mkdir(exist_ok=True)
crops=[(16,50,402,239),(25,48,385,220),(25,42,315,232),(25,40,385,232),(25,40,385,232),(25,83,315,192),(25,58,310,231)]
font_path='C:/Windows/Fonts/malgun.ttf'
title_font=ImageFont.truetype(font_path,44)
subtitle_font=ImageFont.truetype(font_path,24)
note_font=ImageFont.truetype(font_path,20)
thumbs=[]
for index,page in enumerate(pdf):
    im=page.render(scale=3600/page.get_width()).to_pil().convert('RGB')
    im.save(png_dir/f'{index+1:02d}_{manifest["sheets"][index]["name"]}.png')
    # Recompose the KiCad render into a readable 16:9 slide image, preserving all notes.
    slide=Image.new('RGB',(1920,1080),'white')
    draw=ImageDraw.Draw(slide)
    info=manifest['sheets'][index]
    draw.rectangle((0,0,1920,12),fill='#155e75')
    draw.text((65,36),f'{index+1:02d}  {info["title"]}',font=title_font,fill='#17324d')
    draw.text((65,100),info['subtitle'],font=subtitle_font,fill='#466078')
    box=crops[index]
    crop=im.crop(tuple(round(v*im.width/420) for v in box))
    crop.thumbnail((1790,720),Image.Resampling.LANCZOS)
    slide.paste(crop,((1920-crop.width)//2,150+(720-crop.height)//2))
    draw.line((65,892,1855,892),fill='#b8cbd4',width=2)
    for j,note in enumerate(info['notes']):
        draw.text((65,912+j*36),note,font=note_font,fill='#344955')
    draw.text((65,1040),'ARD / KiCad 10 | 동일 라벨 = 전기적 연결 | TBD = 실물 확인 필요 | 핀 표시는 커넥터 순번이 아님',font=note_font,fill='#687d8c')
    slide.save(slide_dir/f'{index+1:02d}_{info["name"]}.png')
    im.thumbnail((700,495))
    tile=Image.new('RGB',(720,530),'#e7e9ee')
    tile.paste(im,((720-im.width)//2,10))
    ImageDraw.Draw(tile).text((15,510),f'{index+1:02d} / {manifest["sheets"][index]["name"]}',fill='black')
    thumbs.append(tile)
contact=Image.new('RGB',(2160,1590),'#d8dce4')
for i,tile in enumerate(thumbs):contact.paste(tile,((i%3)*720,(i//3)*530))
contact.save(OUT/'all_sheets_preview.png')
result={'source_files_unchanged':True,'sheets':7,'source_signal_connections':len(pinmap),
        'exported_signal_nets':len(net_nodes),'all_port_pairs_match':True,'no_extra_or_merged_nets':True,
        'explicitly_unresolved_single_terminal_nets':len(unresolved),
        'erc_errors':0,'erc_warnings':0,'png_width':3600,
        'limitations':['All module pins are passive logical ports; ERC does not verify GPIO electrical capability.',
                       'Supply, ground, motor coil, relay contact and module config terminals unresolved.',
                       'No physical wiring, voltage, current, PCB or hardware test performed.']}
(OUT/'validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2))
