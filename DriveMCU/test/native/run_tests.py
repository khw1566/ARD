"""Run production headers and extracted motor functions with fake hardware on MSVC."""
from pathlib import Path
import os
import subprocess

here = Path(__file__).resolve().parent
project = here.parents[1]
output = here / 'build'
output.mkdir(exist_ok=True)
source = (project / 'src/main.cpp').read_text(encoding='utf-8')
functions = []
for name in ('motorTask', 'emergencyStop'):
    start = source.index('void ' + name + '(')
    while source[source.index(')', start) + 1:].lstrip().startswith(';'):
        start = source.index('void ' + name + '(', start + 1)
    end = source.index('{', start) + 1
    level = 1
    while level:
        if source[end] == '{': level += 1
        elif source[end] == '}': level -= 1
        end += 1
    functions.append(source[start:end])
(output / 'motor_functions.inc').write_text('\n'.join(functions), encoding='utf-8')

vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*',
    '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
if not installation:
    raise SystemExit('MSVC C++ build tools are required for native tests.')
setup = Path(installation) / 'VC/Auxiliary/Build/vcvars64.bat'
lines = ['@echo off', f'call "{setup}" >nul', 'if not "%errorlevel%"=="0" exit /b 1']
for test in ('tests', 'motor_tests'):
    lines += [f'cl /nologo /utf-8 /std:c++17 /EHsc /I"{here / "stubs"}" /I"{project / "include"}" /I"{output}" '
              f'/Fe:"{output / (test + ".exe")}" /Fo:"{output / (test + ".obj")}" "{here / (test + ".cpp")}"',
              'if not "%errorlevel%"=="0" exit /b 1']
batch = output / 'run.cmd'
batch.write_text('\n'.join(lines), encoding='utf-8')
subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=output, check=True)
for test in ('tests', 'motor_tests'):
    subprocess.run([str(output / (test + '.exe'))], cwd=output, check=True)
