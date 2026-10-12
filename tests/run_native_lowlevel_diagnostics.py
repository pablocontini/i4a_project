"""Exercise production maintenance gate and physical-TX diagnostic selection."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default='cc')
args = parser.parse_args()
command = [args.cc, '-Wall', '-Werror']
for directory in ('tests/native/neighbors/mocks', 'tests/native/mocks',
                  'components/internal/ip_diagnostics/include'):
    command += ['-I', str(ROOT / directory)]
source = ROOT / 'tests/native/neighbors/lowlevel_main.c'
if Path(args.cc).stem.lower() == 'tcc':
    subprocess.run(command + ['-run', str(source)], check=True)
else:
    directory = ROOT / 'build/firmware'
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / 'lowlevel_diagnostics_test'
    subprocess.run(command + [str(source), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
