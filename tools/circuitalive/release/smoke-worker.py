"""Exercise analog, XSPICE logic and memory in a relocated compiled installation."""
import argparse,json,os,subprocess
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('worker',type=Path)
parser.add_argument('--scripts',type=Path,required=True)
args=parser.parse_args()
env=dict(os.environ,SPICE_SCRIPTS=str(args.scripts.resolve()))

def run(deck,extra,memories=None):
    configure=dict(op='configure',netlist=deck,step=1e-6,max_step=1e-6,horizon=.01,
                   uic=False,capacity=32,signals=['v(out)'],memories=memories or [],activity_monitors=[])
    requests=[{'op':'hello'},configure,*extra,{'op':'stop'}]
    result=subprocess.run([str(args.worker.resolve())],input=''.join(json.dumps(r)+'\n' for r in requests),
                          text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60,env=env)
    responses=[json.loads(line) for line in result.stdout.splitlines()]
    if result.returncode or len(responses)!=len(requests) or any(not r.get('ok') for r in responses):
        raise RuntimeError(f'Installed simulation worker failed ({result.returncode}): {responses}\n{result.stderr}')
    return [r['values'][0] for r in responses if 'values' in r]

analog=run('Bundled RC test\nV1 in 0 1\nR1 in out 1k\nC1 out 0 1u\n.end\n',
           [{'op':'advance','time':.001},{'op':'snapshot','drain':False}])
assert .99<analog[0]<1.01,analog
digital=run('''Bundled XSPICE inverter test
V1 in 0 0
Ain [in] [din] caadc
.model caadc adc_bridge(in_low=1 in_high=2)
Ainv din dout cainv
.model cainv d_inverter(rise_delay=1n fall_delay=1n)
Aout [dout] [out] cadac
.model cadac dac_bridge(out_low=0 out_high=5)
Rout out 0 1k
.end
''',[{'op':'advance','time':.001},{'op':'snapshot','drain':False},
     {'op':'source','reference':'V1','value':5},{'op':'advance','time':.002},
     {'op':'snapshot','drain':False}])
assert digital[0]>4.9 and abs(digital[1])<.1,digital
memory=run('''Bundled memory test
Vaddr addr 0 0
Vhigh high 0 5
Vlow low 0 0
Aadc [addr high low] [adr on off] adc
.model adc adc_bridge(in_low=1 in_high=2)
Amem [adr] [off] [dout] off on on on mem
.model mem ca_memory(slot=0 read_delay=1n)
Aout [dout] [out] dac
.model dac dac_bridge(out_low=0 out_high=5)
Rout out 0 1k
.end
''',[{'op':'advance','time':.001},{'op':'snapshot','drain':False},
     {'op':'source','reference':'Vaddr','value':5},{'op':'advance','time':.002},
     {'op':'snapshot','drain':False}],
    [dict(id='memory-smoke',label='Memory smoke test',word_bits=1,writable=False,data='0100',known='ffff')])
assert memory[0]>4.9 and abs(memory[1])<.1,memory
print('Installed worker passed analog, XSPICE logic and memory tests.')
