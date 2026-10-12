"""Regression: a searching Station with 0/0 must not blackhole SPI traffic."""
import argparse
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc',default='cc')
p.add_argument('--predicate-source',type=Path,default=ROOT/'components/wireless/device/point_to_point.c')
a=p.parse_args()
cmd=[a.cc,'-Wall','-Werror','-include',str(ROOT/'tests/native/routing/mocks/compiler.h')]
for folder in ['tests/native/p2p/mocks','tests/native/routing/mocks','components/integration/routing_hooks',
               'components/internal/ip_diagnostics/include']:
    cmd+=['-I',str(ROOT/folder)]
for component in ['routing','routing_config','os','sync','shared_state','ring_share','wireless','siblings']:
    cmd+=['-I',str(ROOT/'components/routing'/component/'include')]
sources=[a.predicate_source,ROOT/'components/integration/routing_hooks/routing_hooks.c',ROOT/'tests/native/p2p/p2p_test.c']
out=ROOT/'build/firmware/p2p-native';out.mkdir(parents=True,exist_ok=True)
if Path(a.cc).stem.lower()=='tcc':
    objects=[]
    for i,source in enumerate(sources[:-1]):
        obj=out/f'{i}.o';subprocess.run(cmd+['-c',str(source),'-o',str(obj)],check=True);objects.append(str(obj))
    subprocess.run(cmd+objects+['-run',str(sources[-1])],check=True)
else:
    executable=out/'p2p_test'
    subprocess.run(cmd+list(map(str,sources))+['-o',str(executable)],check=True)
    subprocess.run([str(executable)],check=True)
