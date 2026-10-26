"""Inspect provided binary STL files and derive meshes without changing CAD."""
from pathlib import Path
import json
import struct
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / '영상제작용 CAD파일'
OUT = Path(__file__).resolve().parent
ASSETS = OUT / 'assets'
ASSETS.mkdir(exist_ok=True)
DT = np.dtype([('normal', '<f4', 3), ('vertices', '<f4', (3, 3)), ('attribute', '<u2')])


def load(name):
    path = SOURCE / name
    with path.open('rb') as stream:
        header = stream.read(80)
        count = struct.unpack('<I', stream.read(4))[0]
    if path.stat().st_size != 84 + 50 * count:
        raise ValueError(f'Invalid binary STL size: {name}')
    return np.fromfile(path, DT, offset=84), header


def keys(records):
    # Exact exported triangle coordinates, excluding normal and color metadata.
    return np.ascontiguousarray(records['vertices']).reshape(-1, 9).view('V36').reshape(-1)


def subtract(large, small, name):
    mask = np.isin(keys(large), keys(small))
    matched = int(mask.sum())
    if matched != len(small):
        raise ValueError(f'{name}: expected {len(small)} exact triangles, got {matched}')
    return large[~mask]


def save(records, name):
    with (ASSETS / name).open('wb') as stream:
        stream.write(b'ARD CG derived mesh; source CAD unchanged'.ljust(80, b' '))
        stream.write(struct.pack('<I', len(records)))
        records.tofile(stream)


def components(records, rig=False):
    # Split by shared vertices; these are mesh islands, not CAD part identities.
    points, inverse = np.unique(records['vertices'].reshape(-1, 3), axis=0, return_inverse=True)
    faces = inverse.reshape(-1, 3)
    parent = np.arange(len(points))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for a, b, c in faces:
        ra, rb, rc = find(a), find(b), find(c)
        parent[rb] = ra
        parent[rc] = ra
    roots = np.array([find(int(i)) for i in faces[:, 0]])
    result = []
    rig_parts = {'A': [], 'B': [], 'output': [], 'bottom': []}
    for root in np.unique(roots):
        ids = np.flatnonzero(roots == root)
        verts = records['vertices'][ids].reshape(-1, 3)
        if rig:
            lo, hi = verts.min(0), verts.max(0)
            if hi[0] < -235 and hi[0]-lo[0] < 25:
                role = 'A'
            elif lo[0] > -200 and hi[0]-lo[0] < 25:
                role = 'B'
            elif hi[1] < 65:
                role = 'bottom'
            else:
                role = 'output'
            rig_parts[role].append(records[ids])
        result.append({'triangles': int(len(ids)), 'min': verts.min(0).tolist(),
                       'max': verts.max(0).tolist()})
    if rig:
        for name, chunks in rig_parts.items():
            joined = np.concatenate(chunks)
            if name == 'output':
                save(joined, 'rig_outputCore.stl')
                joined = np.concatenate([joined, plate])
            save(joined, f'rig_{name}.stl')
        assert sum(len(c) for chunks in rig_parts.values() for c in chunks) == len(records)
    return sorted(result, key=lambda r: -r['triangles'])


report = {'source': str(SOURCE), 'units': 'mm, confirmed by user on 2026-10-26',
          'files': [], 'derived': []}
meshes = {}
for path in sorted(SOURCE.glob('*.stl')):
    data, header = load(path.name)
    meshes[path.name] = data
    verts = data['vertices'].reshape(-1, 3)
    lo, hi = verts.min(0), verts.max(0)
    colors, counts = np.unique(data['attribute'], return_counts=True)
    report['files'].append({'file': path.name, 'bytes': path.stat().st_size,
        'triangles': len(data), 'bounds_min': lo.tolist(), 'bounds_max': hi.tolist(),
        'dimensions_in_file_units': (hi-lo).tolist(),
        'header': header.decode('ascii', errors='replace').strip(),
        'attributes': [{'value': int(a), 'triangles': int(b)} for a,b in zip(colors,counts)],
        'finite': bool(np.isfinite(verts).all())})

gear = meshes['베벨기어 조립체.stl']
plate = subtract(meshes['베벨 기어 및 플레이트.stl'], gear, 'plate')
other_gears = subtract(gear, meshes['단일 베벨기어.stl'], 'remaining bevel assembly')
upper_shell = subtract(meshes['상부이송구조.stl'], meshes['베벨 기어 및 플레이트.stl'], 'upper structure')
bins = subtract(meshes['ARD_Model_v2.stl'], meshes['박스제거.stl'], 'bins')
for name, data in [('tray.stl',plate), ('bevel_remaining.stl',other_gears),
                   ('upper_without_core.stl',upper_shell), ('bins.stl',bins)]:
    save(data, name)
    report['derived'].append({'file': name, 'triangles': len(data)})
report['bevel_mesh_islands'] = components(gear, rig=True)
report['plate_mesh_islands'] = components(plate)
(OUT / 'cad_inventory.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'STL_files':len(meshes),'derived':report['derived'],
    'bevel_mesh_islands':report['bevel_mesh_islands'][:12],
    'plate_mesh_islands':report['plate_mesh_islands'][:6]},ensure_ascii=False),flush=True)
