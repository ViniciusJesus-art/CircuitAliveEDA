"""Exercise analog, XSPICE, memory and firmware in a relocated installation."""
import argparse,json,os,subprocess
from pathlib import Path
from programmable_assets import validate_programmable_assets

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('worker',type=Path)
parser.add_argument('--scripts',type=Path,required=True)
args=parser.parse_args()
env=dict(os.environ,SPICE_SCRIPTS=str(args.scripts.resolve()))

def run(deck,extra,memories=None,devices=None,signals=None,accelerate=False):
    configure=dict(op='configure',netlist=deck,step=1e-6,max_step=1e-6,horizon=.01,
                   uic=False,capacity=32,signals=signals or ['v(out)'],memories=memories or [],
                   programmable_devices=devices or [],activity_monitors=[],accelerate_expressions=accelerate)
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
expression=run('Compiled source test\nV1 in 0 2\nB1 out 0 V={tanh(v(in))*5}\nR1 out 0 1k\n.end\n',
               [{'op':'advance','time':.001},{'op':'snapshot','drain':False}],accelerate=True)
assert 4.819<expression[0]<4.821,expression
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

stage=args.worker.resolve().parent.parent
validate_programmable_assets(stage)
for name,pin,words,clock in (
    ('mcu_arduino_uno_r3','PB5',[0xe200,0xb904,0xe200,0xb905,0xcfff],16e6),
    ('mcu_pic16f84a_p','RB0',[0x1683,0x1006,0x1283,0x1406,0x2804],4e6)):
    file=stage/'share/circuitalive/models'/(name+'.casim')
    descriptor=json.loads(file.read_text(encoding='utf-8'))['runtime']
    source='\n;\n'.join((file.parent/module).read_text(encoding='utf-8')
                         for module in descriptor['modules'])
    firmware=bytearray([255])*descriptor['firmware_bytes']
    for i,word in enumerate(words):firmware[2*i:2*i+2]=word.to_bytes(2,'little')
    device=dict(id='U1',engine='javascript',device=descriptor['device'],source=source,
                clock_hz=clock,quantum=1e-6,firmware=firmware.hex(),eeprom='',options='{}',
                pins=[p['id'] for p in descriptor['pins']],bindings=[
                    dict(id=p,signal=signal,high='',low='',pullup='')
                    for p,signal in (('VCC','v(vcc)'),('GND','v(ground_probe)'),('RESET','v(vcc)'))]+[
                    dict(id=pin,signal='v(out)',high='',low='',pullup='',driver_slot=0)])
    output=run('''Bundled firmware GPIO test
Vcc vcc 0 5
Bgnd ground_probe 0 V=0
Adriver %gd out 0 %gd out vcc gpio
.model gpio ca_programmable_pin(slot=0)
Rload out 0 10k
.end
''',[{'op':'advance','time':20e-6},{'op':'snapshot','drain':False},
     {'op':'advance','time':40e-6},{'op':'snapshot','drain':False}],
     devices=[device],signals=['v(out)','v(vcc)','v(ground_probe)'])
    assert len(output)==2 and all(4.9<v<5.01 for v in output),(name,output)
print('Installed worker passed analog, XSPICE, memory, AVR and PIC firmware tests.')
