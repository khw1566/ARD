"""Compare the KiCad-exported connectivity to source pinmap and new power nets."""
from pathlib import Path
import csv,json,xml.etree.ElementTree as ET,re
from pypdf import PdfReader
OUT=Path(__file__).resolve().parent
OLD=Path('C:/Users/work/Desktop/ETC/ARD_software/results/kicad_legacy_261017')
tree=ET.parse(OUT/'ARD_2Sheet.net.xml')
actual=[]
for net in tree.findall('.//nets/net'):
    if net.attrib['name'].startswith('unconnected-'):continue
    actual.append(frozenset((n.attrib['ref'],n.attrib['pin']) for n in net.findall('node')))
expected=json.loads((OUT/'expected_nets.json').read_text())
planned=[frozenset(tuple(n) for n in ns) for sheet in expected.values() for ns in sheet.values()]
assert set(actual)==set(planned),{'missing':[list(x) for x in set(planned)-set(actual)],'extra':[list(x) for x in set(actual)-set(planned)]}
mapping={'U301':'U11','U302':'U12','U303':'U13','U401':'U21','U402':'U22','U403':'U23','U404':'U24','U405':'U25','J401':'BZ1','U501':'U51','U502':'U52','U503':'U53','K501':'K1','U504':'U55','U601':'U31'}
mcus={'ControlMCU':'U60','DriveMCU':'U10','Robot3AxisMCU':'U50','ImageSenderMCU':'U30','HMI_MCU':'U70'}
retained=0;removed=[]
with (OLD/'pinmap.csv').open(encoding='utf-8-sig',newline='') as f:
    for r in csv.DictReader(f):
        if r['board']=='DriveMCU' and r['terminal'] in ['SW1','SW2','POT_WIPER']:
            removed.append({'terminal':r['terminal'],'gpio':r['gpio'],'reason':'current DriveMCU board.h declares manual inputs unused'});continue
        if r['board'] in ['ControlMCU','HMI_MCU']:
            ref='J60' if r['board']=='ControlMCU' else 'J70';pin=r['device_ref']+'_'+r['terminal']
        else:ref=mapping[r['device_ref']];pin=r['terminal']
        pair={(mcus[r['board']],r['gpio']),(ref,pin)}
        assert any(pair<=net for net in actual),(r,pair)
        retained+=1
assert retained==75
power=frozenset([('U1','OUT+'),('U10','5V'),('U30','5V')]+[(f'U2{i}','VCC') for i in range(1,6)])
assert power in actual
assert frozenset([('J2','+'),('U40','EXT+')]) in actual
assert frozenset([('BT1','+'),('U40','BAT+')]) in actual
assert not any(('U40','EXT+') in n and ('U10','5V') in n for n in actual)
report=(OUT/'erc.rpt').read_text(encoding='utf8')
counts=re.search(r'Errors (\d+)\s+Warnings (\d+)',report)
assert counts and counts[1]=='0'
assert all(x=='isolated_pin_label' for x in re.findall(r'^\[([^]]+)\]',report,re.M))
pdf=PdfReader(OUT/'ARD_2Sheet_회로도.pdf');assert len(pdf.pages)==2
result={'native_sheets':len(tree.findall('.//design/sheet')),'pdf_pages':len(pdf.pages),'all_exported_nets_match':True,'source_signal_connections_retained':retained,'current_unused_inputs_omitted':removed,'power_topology_matches_user_description':True,'erc_errors':int(counts[1]),'erc_warnings':int(counts[2]),'warning_type':'isolated_pin_label: unresolved TBD terminals only','electrical_review':'consistency check only; no datasheet rating, PCB, hardware or firmware-build validation'}
(OUT/'validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(result,ensure_ascii=False))
