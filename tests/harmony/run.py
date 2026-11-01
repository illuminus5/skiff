#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deterministic HARMONY integration tests; no device access or private recordings.
Run tools/build_harmony.py first to generate build/gen, then this script.
"""
from pathlib import Path
import math, os, subprocess, sys, wave, array, json
R=Path(__file__).resolve().parents[2];T=Path(__file__).resolve().parent;O=R/'build/harmony-tests';O.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(R/'tools'));import sampleio
samples=[int(7000*math.sin(2*math.pi*261.625565*i/22050)+1800*math.sin(4*math.pi*261.625565*i/22050)) for i in range(79380)]
h,d=sampleio.user_slot('TESTOOH',[(samples,60,None,None)])
(O/'source.hdr').write_bytes(h);(O/'source.bin').write_bytes(d)
flags=['-DFELUCCA_HARMONY=1','-DVS_METRICS=0',*[f'-I{p}' for p in [O,R/'build/gen',R/'firmware/src',R/'tests']]]
cc=os.environ.get('CC','cc')
def compile(name,extra=()):
 print('compile',name,flush=True);subprocess.run([cc,'-O1','-g','-w',*flags,*extra,str(T/(name+'.c')),'-lm','-o',str(O/name)],check=True)
def run(name,args=(),env=None):
 subprocess.run([str(O/name),str(O/'source'),*map(str,args)],env=dict(os.environ,**(env or {})),check=True)
compile('engine');run('engine',[O/'engine.wav']);run('engine',[O/'oneshot.wav'],{'H_ONESHOT':'1'});run('engine',[O/'missing.wav'],{'H_MISSING':'1'})
for name in ['loop','transitions','contracts','retired','stress']:
 compile(name);run(name)
for mode in ['1','2']:run('stress',env={'H_FEATURES':mode})
compile('engine',['-DVOCAL_C1_TEST=1','-pthread']);ref=None
for mask in ['off','rot','auto','1','62']:
 env={k:v for k,v in os.environ.items() if k not in ['C1_OFF','C1_MASK']};env.update({'C1_OFF':'1'} if mask=='off' else {'C1_MASK':mask})
 p=O/f'worker-{mask}.wav';subprocess.run([str(O/'engine'),str(O/'source'),str(p)],env=env,check=True)
 if ref is None:ref=p.read_bytes()
 else:assert p.read_bytes()==ref,mask
 print('worker mask',mask,'exact',flush=True)
for off in [False,True]:
 env={k:v for k,v in os.environ.items() if k not in ['C1_OFF','C1_MASK']};env['H_ACCOMP']='1'
 if off:env['C1_OFF']='1'
 subprocess.run([str(O/'engine'),str(O/'source'),str(O/f'budget-{8 if off else 12}.wav')],env=env,check=True)
# Render the actual UI and exercise USER/project roundtrips against simulated storage.
s=(R/'tests/ui_render.c').read_text().replace('int main(int argc, char **argv)','int render_all_main(int argc, char **argv)');(O/'ui_render_preview.c').write_text(s)
(O/'ppm').mkdir(exist_ok=True);compile('ui');subprocess.run([str(O/'ui'),str(O/'source'),str(O)],check=True)
s=(R/'tests/slice_test.c').read_text().replace('int main(int argc, char **argv)','int slice_test_main(int argc, char **argv)');(O/'slice_store_preview.c').write_text(s)
compile('sample_store');run('sample_store')
# Existing fixed-point DSP relies on signed wrap/shift/XIP conventions. These exclusions
# match the existing integration harness; no claim of strict coverage for them.
if os.environ.get('SANITIZE','1')!='0':
 for name in ['loop','retired']:
  compile(name,['-fsanitize=address,undefined','-fno-sanitize=shift,signed-integer-overflow,array-bounds']);run(name)
# Check audible pitch movement on rendered synthetic audio, with standard-library analysis.
compile('features')
freq=246.94165*2**(35/1200)
x=[int(9000*math.sin(2*math.pi*freq*i/22050)+2200*math.sin(4*math.pi*freq*i/22050)) for i in range(79380)]
h,d=sampleio.user_slot('TUNETEST',[(x,60,None,None)])
(O/'tune.hdr').write_bytes(h);(O/'tune.bin').write_bytes(d)
def power(values,hz):
    c=2*math.cos(2*math.pi*hz/44100);a=b=0.0
    for x in values:a,b=x+c*a-b,a
    return a*a+b*b-c*a*b
# The lead must audibly follow C4 -> E4 in either voicing; backing must not pin it.
lead_renders=[]
for backing in [False,True]:
    dest=O/f'lead-{int(backing)}.wav'
    env=dict(os.environ)
    if backing:env['H_LEAD_BACKING']='1'
    else:env.pop('H_LEAD_BACKING',None)
    subprocess.run([str(O/'features'),str(O/'source'),str(dest),'0'],env=env,check=True)
    lead_renders.append(dest.read_bytes())
assert lead_renders[0]==lead_renders[1], 'BACKING must not change the lead pitch path'
with wave.open(str(O/'lead-1.wav')) as f:lead=array.array('h',f.readframes(f.getnframes()))
if sys.byteorder!='little':lead.byteswap()
for second,expected in [(1,261.625565),(2.5,329.627557)]:
    start=int(second*44100);n=16384
    values=[(lead[2*(start+i)]+lead[2*(start+i)+1])/2*(.5-.5*math.cos(2*math.pi*i/(n-1))) for i in range(n)]
    hz=max((expected-8+i*.25 for i in range(65)),key=lambda hz:power(values,hz))
    assert abs(1200*math.log2(hz/expected))<20,(second,hz,expected)
dest=O/'lead-high.wav'
subprocess.run([str(O/'features'),str(O/'source'),str(dest),'0'],env=dict(os.environ,H_LEAD_BACKING='1',H_LEAD_HIGH='1'),check=True)
with wave.open(str(dest)) as f:high=array.array('h',f.readframes(f.getnframes()))
if sys.byteorder!='little':high.byteswap()
for second,expected in [(1,523.25113),(2.5,659.255114)]:
    start=int(second*44100);n=16384
    values=[(high[2*(start+i)]+high[2*(start+i)+1])/2*(.5-.5*math.cos(2*math.pi*i/(n-1))) for i in range(n)]
    hz=max((expected-8+i*.25 for i in range(65)),key=lambda hz:power(values,hz))
    assert abs(1200*math.log2(hz/expected))<20,(second,hz,expected)
print('Rendered lead follows C4 -> E4 and C5 -> E5; FOLLOW/BACKING lead paths match.',flush=True)
measurements={}
for mode in range(3):
    wav=O/f'tune-{mode}.wav';subprocess.run([str(O/'features'),str(O/'tune'),str(wav),str(mode)],check=True)
    with wave.open(str(wav)) as f:raw=array.array('h',f.readframes(f.getnframes()))
    if sys.byteorder!='little':raw.byteswap()
    n=32768;values=[((raw[2*(44100+i)]+raw[2*(44100+i)+1])/2)*(0.5-0.5*math.cos(2*math.pi*i/(n-1))) for i in range(n)]
    coarse=max((225+i*.5 for i in range(101)),key=lambda hz:power(values,hz))
    hz=max((coarse-.5+i*.05 for i in range(21)),key=lambda hz:power(values,hz))
    measurements[mode]={'hz':hz,'cents_from_B3':1200*math.log2(hz/246.94165)}
assert abs(measurements[0]['cents_from_B3']-35)<12,measurements
assert abs(measurements[1]['cents_from_B3'])<abs(measurements[0]['cents_from_B3']),measurements
assert abs(measurements[2]['cents_from_B3'])<10,measurements
(O/'tuning.json').write_text(json.dumps(measurements,indent=2)+'\n');print('Rendered pitch correction:',measurements,flush=True)

print('HARMONY integration tests passed.',flush=True)
