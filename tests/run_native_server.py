"""Inject a listener bind failure into the production TCP server."""
import argparse,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cc',default='cc');p.add_argument('--run',action='store_true');a=p.parse_args()
cmd=[a.cc,'-Wall','-Werror','-I',str(ROOT/'tests/native/server/mocks'),'-I',str(ROOT/'components/routing/os/include')]
src=str(ROOT/'tests/native/server/server_retry_test.c')
if a.run:subprocess.run(cmd+['-run',src],check=True)
else:
 out=ROOT/'build/firmware/server_retry_test';out.parent.mkdir(parents=True,exist_ok=True)
 subprocess.run(cmd+[src,'-o',str(out)],check=True);subprocess.run([str(out)],check=True)
