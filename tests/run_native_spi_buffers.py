"""Run real SPI polling/ownership tests with zero and eight spare RX buffers."""
import argparse
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc',default='cc',help='GCC/Clang or TinyCC executable')
a=p.parse_args()
build=ROOT/'build/native-spi-buffers';build.mkdir(parents=True,exist_ok=True)
main=build/'main.c'
names=['test_spi_keeps_dma_receives_queued_and_drops_truncated_frames',
       'test_spi_delayed_consumer_retains_ownership',
       'test_spi_crc_at_completion_is_distinct_from_buffer_mutation']
source='#include <stdio.h>\n'+''.join(f'extern int {n}(void);\n' for n in names)
source+='int main(void){int r;\n'
for n in names:source+=f'r={n}();printf("{n}: %d\\n",r);if(r)return r;\n'
source+='return 0;}\n';main.write_text(source,encoding='utf-8')
for extra in (0,8):
    cmd=[a.cc,'-Wall','-Werror','-include',str(ROOT/'tests/native/routing/mocks/compiler.h'),
         f'-DCONFIG_SPI_RX_EXTRA_BUFFERS={extra}']
    for path in ('tests/native/spi/mocks','tests/native/neighbors/mocks',
                 'tests/native/mocks','components/internal/link_diagnostics/include'):
        cmd+=['-I',str(ROOT/path)]
    test=ROOT/'tests/native/neighbors/spi_test.c'
    if Path(a.cc).stem.lower()=='tcc':
        obj=build/f'spi-{extra}.o'
        subprocess.run(cmd+['-c',str(test),'-o',str(obj)],check=True)
        subprocess.run([a.cc,str(obj),'-run',str(main)],check=True)
    else:
        binary=build/f'spi-{extra}'
        subprocess.run(cmd+[str(test),str(main),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    print(f'PASS: extra={extra}, three SPI cases')