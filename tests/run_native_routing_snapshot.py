"""Exercise production read-only route snapshots with lock contention."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default='cc')
args = parser.parse_args()
command = [args.cc, '-Wall', '-Werror', '-include',
           str(ROOT / 'tests/native/routing/mocks/compiler.h')]
for component in ('routing', 'routing_config', 'os', 'sync', 'shared_state', 'ring_share', 'wireless', 'siblings'):
    command += ['-I', str(ROOT / 'components/routing' / component / 'include')]
sources = [ROOT / 'components/routing/routing/src/routing_diagnostics.c',
           ROOT / 'components/routing/routing/src/routing_table.c',
           ROOT / 'tests/native/routing/snapshot_test.c']
if Path(args.cc).stem.lower() == 'tcc':
    directory = ROOT / 'build/firmware/snapshot-native'
    directory.mkdir(parents=True, exist_ok=True)
    objects = []
    for index, source in enumerate(sources[:-1]):
        object_path = directory / f'{index}.o'
        subprocess.run(command + ['-c', str(source), '-o', str(object_path)], check=True)
        objects.append(str(object_path))
    subprocess.run(command + objects + ['-run', str(sources[-1])], check=True)
else:
    directory = ROOT / 'build/firmware'
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / 'routing_snapshot_test'
    subprocess.run(command + list(map(str, sources)) + ['-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
