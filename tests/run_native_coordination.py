"""Run production sync and shared-state through a five-board loss simulator."""
import argparse,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cc',default='cc');p.add_argument('--run',action='store_true');a=p.parse_args()
cmd=[a.cc,'-Wall','-Werror','-include',str(ROOT/'tests/native/routing/mocks/compiler.h')]
for inc in ['sync/include','ring_share/include','siblings/include','routing_config/include','os/include','shared_state/include']:cmd+=['-I',str(ROOT/'components/routing'/inc)]
sources=[ROOT/'tests/native/coordination/coordination_test.c']+[ROOT/'components/routing'/s for s in ['sync/src/sync.c','sync/src/leader.c','sync/src/follower.c','shared_state/src/shared_state.c']]
if a.run:
 objdir=ROOT/'build/firmware/coordination-native';objdir.mkdir(parents=True,exist_ok=True)
 objects=[]
 for i,source in enumerate(sources[1:]):
  obj=objdir/f'{i}.o';subprocess.run(cmd+['-c',str(source),'-o',str(obj)],check=True);objects.append(str(obj))
 subprocess.run(cmd+objects+['-run',str(sources[0])],check=True)
else:
 out=ROOT/'build/firmware/coordination_tests.exe';out.parent.mkdir(parents=True,exist_ok=True)
 subprocess.run(cmd+list(map(str,sources))+['-o',str(out)],check=True);subprocess.run([str(out)],check=True)
