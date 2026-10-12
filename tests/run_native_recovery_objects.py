"""Run real recovery callbacks with TinyCC without loading a DLL."""
import argparse
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
d = ROOT / 'build/firmware/return-route-native'
d.mkdir(parents=True, exist_ok=True)
cmd = [args.cc, '-Wall', '-Werror', '-include', str(ROOT / 'tests/native/routing/mocks/compiler.h')]
parts = ['tests/native/routing/mocks'] + [f'components/routing/{c}/include' for c in ('routing','routing_config','os','ring_share','shared_state','sync','wireless','siblings')] + ['components/routing/routing/src','components/routing/routing/src/forwarder']
for part in parts:
    cmd += ['-I', str(ROOT / part)]
harness = ROOT / 'tests/native/routing/recovery_test.c'
sources = [harness] + [ROOT / 'components/routing/routing/src' / p for p in ('forwarder/forwarder.c','forwarder/peer.c','forwarder/sibling.c','root.c','home.c','routing.c','routing_table.c','utils.c')]
objects = []
for i, source in enumerate(sources):
    obj = d / f'{i}.o'
    subprocess.run(cmd + ['-c', str(source), '-o', str(obj)], check=True)
    objects.append(str(obj))
names = re.findall(r'EXPORT int (test_\w+)\(void\)', harness.read_text())
main = d / 'main.c'
code = '#include <stdio.h>\n' + ''.join(f'extern int {n}(void);\n' for n in names)
code += 'int main(void){int line; int failures=0;\n'
for name in names:
    code += f'line={name}(); printf("{name}: %d\\n",line); if(line) failures++;\n'
code += 'return failures;}\n'
main.write_text(code)
subprocess.run(cmd + objects + ['-run', str(main)], check=True)
