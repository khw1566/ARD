"""Inspect provided binary STL files and derive meshes without changing CAD."""
from pathlib import Path
import hashlib
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


def mesh_islands(records):
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
    for root in np.unique(roots):
        ids = np.flatnonzero(roots == root)
        yield records[ids]


def components(records):
    result = []
    for island in mesh_islands(records):
        verts = island['vertices'].reshape(-1, 3)
        result.append({'triangles': len(island), 'min': verts.min(0).tolist(),
                       'max': verts.max(0).tolist()})
    return sorted(result, key=lambda r: -r['triangles'])


def split_planetary(records):
    """Retain the supplied reducer's exact geometry and initial tooth phase.

    Motion roles are inferred from this export's axis, gear layer, and radial
    centers, cross-checked with the supplied CAD videos. Tooth counts and the
    fixed/input/output members were confirmed by the user on 2026-10-26.
    """
    islands = list(mesh_islands(records))
    details = []
    for index, island in enumerate(islands):
        verts = island['vertices'].reshape(-1, 3)
        lo, hi = verts.min(0), verts.max(0)
        details.append((index, (lo + hi) / 2, hi - lo))
    suns = [(i, center) for i, center, size in details
            if 26 < size[0] < 28 and 26 < size[2] < 28 and len(islands[i]) == 1884]
    if len(suns) != 1:
        raise ValueError('Expected one recognizable sun gear in reducer CAD')
    sun_id, pivot = suns[0]
    planet_ids = [(i, center) for i, center, size in details
                  if 26 < size[0] < 28 and 26 < size[2] < 28
                  and abs(size[1] - 10) < .01 and 23.8 < np.linalg.norm((center-pivot)[[0, 2]]) < 24.2]
    if len(planet_ids) != 4:
        raise ValueError('Expected four planets on a 24 mm pitch-center orbit')
    planet_ids.sort(key=lambda item: float(np.arctan2(item[1][2]-pivot[2], item[1][0]-pivot[0])))
    roles = {name: [] for name in ['sun', 'carrier', 'housing', 'fixed']}
    for j in range(4):
        roles[f'planet{j}'] = []
        roles[f'mount{j}'] = []
    housing_id = max(range(len(islands)), key=lambda i: len(islands[i]))
    gear_roles = {i: f'planet{j}' for j, (i, _) in enumerate(planet_ids)}
    role_sources = {}
    for index, center, size in details:
        radial = np.linalg.norm((center-pivot)[[0, 2]])
        distances = [np.linalg.norm((center-p)[[0, 2]]) for _, p in planet_ids]
        closest = int(np.argmin(distances))
        if index == sun_id:
            role = 'sun'
        elif index in gear_roles:
            role = gear_roles[index]
        elif index == housing_id:
            role = 'housing'
        elif distances[closest] < .15 and center[1] > pivot[1] - .1:
            role = f'mount{closest}'
        elif radial < 2 and abs(center[1]-pivot[1]) < 6:
            role = 'sun'  # Input gear's central fastener.
        elif (radial < 1 and size[1] > 13 and center[1] > pivot[1]) or (radial < 9 and center[1] > pivot[1] + 12 and len(islands[index]) == 984):
            role = 'carrier'
        else:
            role = 'fixed'
        roles[role].append(islands[index])
        role_sources.setdefault(role, []).append(index)
    if any(not chunks for chunks in roles.values()):
        raise ValueError('Empty planetary role; inspect the changed source export')
    parts = []
    for role, chunks in roles.items():
        data = np.concatenate(chunks)
        filename = f'planetary_{role}.stl'
        save(data, filename)
        parts.append({'role': role, 'file': filename, 'triangles': len(data),
                      'source_islands': role_sources[role]})
    if sum(part['triangles'] for part in parts) != len(records):
        raise ValueError('Planetary mesh partition is incomplete')
    result = {
        'source': '커버와 베어링이 제거된 감속기.stl',
        'source_sha256': hashlib.sha256((SOURCE / '커버와 베어링이 제거된 감속기.stl').read_bytes()).hexdigest(),
        'pivot': pivot.tolist(), 'axis': 'Y',
        'planet_centers': [p.tolist() for _, p in planet_ids], 'parts': parts,
        'source_triangles': len(records), 'partition_triangles': sum(p['triangles'] for p in parts),
        'teeth': {'sun': 16, 'planet': 16, 'ring': 48}, 'planet_count': 4,
        'configuration': {'fixed': 'ring', 'input': 'sun', 'output': 'carrier'},
        'reduction': 4, 'configuration_evidence': 'Confirmed directly by user on 2026-10-26',
        'role_evidence': 'CAD mesh islands, radial centers, and user-provided front/oblique videos',
        'motion': {'carrier': 'sun / 4', 'planet_absolute_spin': '-sun / 2',
                   'planet_spin_relative_to_carrier': '-3 * sun / 4'},
        'limits': 'Rigid ideal kinematics; no tooth contact, backlash, load, or hardware verification',
    }
    (OUT / 'planetary_inventory.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return result


def split_differential(assembly, meshes):
    """Partition one CAD pose by explicit part exports, never by mesh islands.

    Shared vertices can join touching CAD parts into one island. In particular,
    the cross carrier must not inherit the upper bevel gear's axial rotation.
    The supplied A/B-only exports have a different tooth phase, so retain A/B
    triangles from this assembly after subtracting the three exact exports.
    """
    assembly_keys = keys(assembly)
    assigned = np.zeros(len(assembly), dtype=bool)
    parts = {}
    sources = {
        'cross': '베벨기어 중심 십자조인.stl',
        'output': '베벨기어 상단부.stl',
        'bottom': '베벨기어 하단부.stl',
    }
    for role, source in sources.items():
        data = meshes[source]
        mask = np.isin(assembly_keys, keys(data))
        if int(mask.sum()) != len(data) or np.any(mask & assigned):
            raise ValueError(f'{source}: not a disjoint exact subset of the bevel assembly')
        parts[role] = assembly[mask]
        assigned |= mask

    remaining = assembly[~assigned]
    verts = remaining['vertices']
    left = verts[:, :, 0].max(1) < -235
    right = verts[:, :, 0].min(1) > -200
    if not np.all(left ^ right):
        raise ValueError('Unassigned bevel faces: explicit cross/upper/lower exports do not cover the mechanism')
    parts['A'], parts['B'] = remaining[left], remaining[right]
    if sum(map(len, parts.values())) != len(assembly) or any(not len(p) for p in parts.values()):
        raise ValueError('Differential partition is incomplete or empty')

    files = {'A': 'rig_A.stl', 'B': 'rig_B.stl', 'cross': 'rig_cross.stl',
             'output': 'rig_outputCore.stl', 'bottom': 'rig_bottom.stl'}
    result = []
    for role, data in parts.items():
        save(data, files[role])
        result.append({'role': role, 'file': files[role], 'triangles': len(data),
                       'source': sources.get(role, '베벨기어 조립체.stl'),
                       'method': 'exact triangle subset; common assembly pose'})
    return {'parts': result, 'assembly_triangles': len(assembly),
            'partition_triangles': sum(map(len, parts.values())),
            'overlap_triangles': 0,
            'hierarchy': 'root: A, B, carrier(cross, output(plate), bottom)',
            'carrier_rotation': 'Rx(tilt); no output yaw',
            'output_rotation': 'carrier * Ry(yaw)',
            'bottom_rotation': 'carrier * Ry(-yaw)'}


report = {'source': str(SOURCE), 'units': 'mm, confirmed by user on 2026-10-26',
          'files': [], 'derived': []}
meshes = {}
for path in sorted(SOURCE.glob('*.stl')):
    data, header = load(path.name)
    meshes[path.name] = data
    verts = data['vertices'].reshape(-1, 3)
    if not len(data) or not np.isfinite(verts).all():
        raise ValueError(f'Empty or non-finite STL: {path.name}')
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
report['differential_rig'] = split_differential(gear, meshes)
report['planetary_rig'] = split_planetary(meshes['커버와 베어링이 제거된 감속기.stl'])
report['bevel_mesh_islands'] = components(gear)
report['plate_mesh_islands'] = components(plate)
(OUT / 'cad_inventory.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'STL_files':len(meshes),'derived':report['derived'],
    'differential_rig':report['differential_rig'],
    'bevel_mesh_islands':report['bevel_mesh_islands'][:12],
    'plate_mesh_islands':report['plate_mesh_islands'][:6]},ensure_ascii=False),flush=True)
