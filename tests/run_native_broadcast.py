"""Exercise the production broadcast handler under a blocked sibling queue."""
import argparse
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cc",default="cc")
    p.add_argument("--run",action="store_true",help="Use TinyCC source execution")
    a=p.parse_args()
    cmd=[a.cc,"-Wall","-Werror","-I",str(ROOT/"tests/native/broadcast/mocks")]
    source=str(ROOT/"tests/native/broadcast/ordering_test.c")
    if a.run:subprocess.run(cmd+["-run",source],check=True)
    else:
        build=ROOT/"app/build/native-tests";build.mkdir(parents=True,exist_ok=True)
        target=build/("broadcast_tests.exe" if sys.platform=="win32" else "broadcast_tests")
        subprocess.run(cmd+[source,"-o",str(target)],check=True)
        subprocess.run([str(target)],check=True)
if __name__=="__main__":main()
