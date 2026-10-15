"""Compile the firmware's actual C-compatible validators with MSVC and run them."""
from pathlib import Path
import os
import subprocess

here = Path(__file__).resolve().parent
output = here / 'build'
output.mkdir(exist_ok=True)
vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
if not installation:
    raise SystemExit('MSVC C++ build tools are required.')
setup = Path(installation) / 'VC/Auxiliary/Build/vcvars64.bat'
batch = output / 'run.cmd'
batch.write_text('\n'.join(['@echo off', f'call "{setup}" >nul',
    'if not "%errorlevel%"=="0" exit /b 1',
    f'cl /nologo /utf-8 /std:c++17 /EHsc /I"{here.parent / "src"}" /Fe:"{output / "settings_test.exe"}" /Fo:"{output / "settings_test.obj"}" "{here / "settings_test.cpp"}"',
    'if not "%errorlevel%"=="0" exit /b 1']), encoding='utf-8')
subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=output, check=True)
subprocess.run([str(output / 'settings_test.exe')], cwd=output, check=True)
