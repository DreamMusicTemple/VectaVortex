"""Extra named XFX/LUA/SID presets: restored legacy set (minus Jungle) + 8 new IDM XFX presets. Imported by make_recipes.export()."""
import copy, json, os
def _base():
    import make_recipes as m
    return m.ordered([r for r in m.NAMED if r['name'] == 'AYFXTWN'][0])
base = _base()
def pat(*hits,n=16): return ''.join('x' if i in hits else '.' for i in range(n))
def pat(*hits,n=16): return ''.join('x' if i in hits else '.' for i in range(n))
def drum(name,seed,bpm,desc,lanes,rep_ranges,fxmix):
    r=copy.deepcopy(base); r['name']=name; r['seed']=seed; r['desc']=desc
    r['set']=[x for x in r['set'] if x[0]!='bpm']+[['bpm',bpm]]
    r['xfx']['bank']=name; r['xfx']['range']=[36,51]; r['xfx']['mode']='band'
    for e in r['xfx']['palette']:
        for o in e['o']:
            if o[0]=='SMP.oscs.0.env.rep': o[1]={"p":rep_ranges}
    r['fx']=[dict(f) for f in r['fx']]
    r['drumSeq']={'xfxBase':36,'lanes':lanes}
    return r

L1=[{'len':16,'steps':pat(0,3,10)},{'len':16,'steps':pat(6,9,13)},{'len':16,'steps':pat(2,14)},
    {'len':16,'steps':pat(4,12)},{'len':16,'steps':pat(7,15)},{'len':12,'steps':pat(5,11,n=12)},
    {'len':16,'steps':pat(*range(0,16,2))},{'len':16,'steps':pat(1,3,5,7,9,11,13,15)},{'len':16,'steps':pat(*range(0,16))},{'len':16,'steps':pat(12,13,14,15)},
    {'len':16,'steps':pat(2,5,9)},{'len':16,'steps':pat(8)},{'len':16,'steps':pat(3,11)},{'len':16,'steps':pat(0,7,10,15)},{'len':16,'steps':pat(6,14)},{'len':16,'steps':pat(1,10)}]
L2=[{'len':15,'steps':pat(0,4,9,n=15)},{'len':13,'steps':pat(3,7,10,n=13)},{'len':16,'steps':pat(0,5,10,15)},
    {'len':16,'steps':pat(4,12)},{'len':14,'steps':pat(2,8,11,n=14)},{'len':11,'steps':pat(6,9,n=11)},
    {'len':7,'steps':pat(0,2,4,6,n=7)},{'len':9,'steps':pat(1,3,5,7,8,n=9)},{'len':16,'steps':pat(*range(1,16,2))},{'len':10,'steps':pat(0,1,5,8,n=10)},
    {'len':12,'steps':pat(3,6,9,n=12)},{'len':16,'steps':pat(7,15)},{'len':5,'steps':pat(0,3,n=5)},{'len':32,'steps':pat(0,3,8,13,17,22,26,29,n=32)},{'len':6,'steps':pat(2,5,n=6)},{'len':8,'steps':pat(1,6,n=8)}]
L3=[{'len':32,'steps':pat(0,3,6,10,16,19,22,25,28,n=32)},{'len':32,'steps':pat(7,15,23,27,31,n=32)},{'len':16,'steps':pat(0,1,8,9)},
    {'len':32,'steps':pat(8,24,30,n=32)},{'len':32,'steps':pat(12,13,14,15,28,31,n=32)},{'len':24,'steps':pat(4,10,18,n=24)},
    {'len':32,'steps':pat(*range(0,32),n=32)},{'len':16,'steps':pat(*range(0,16,3))},{'len':32,'steps':pat(2,3,6,7,10,11,14,15,18,19,26,27,30,31,n=32)},{'len':20,'steps':pat(0,9,15,n=20)},
    {'len':32,'steps':pat(1,5,17,21,29,n=32)},{'len':32,'steps':pat(0,16,n=32)},{'len':32,'steps':pat(11,13,23,n=32)},{'len':32,'steps':pat(3,4,19,20,25,n=32)},{'len':28,'steps':pat(5,12,22,n=28)},{'len':32,'steps':pat(9,25,26,27,n=32)}]
def lead(name,seed,bpm,desc,tables,pitches,grid,dens,leap,ratchet,bars,names,quant,cut):
    pal=[]
    for i,t in enumerate(tables):
        pal.append({"en":{"WT":True,"FM":False,"SMP":False,"PRO":False,"SID":False},"o":[
          ["WT.oscs.0.table",t],["WT.oscs.0.level",{"r":[0.55,0.8]}],["WT.oscs.0.unison",{"ri":[1,4]}],["WT.oscs.0.detune",{"r":[4,16]}],
          ["WT.oscs.0.filter.cutoff",{"rl":[cut[0],cut[1]]}],["WT.oscs.0.filter.reso",{"r":[6,34]}],["WT.oscs.0.filter.on",True],
          ["WT.env.a",0.002],["WT.env.d",{"r":[0.08,0.25]}],["WT.env.s",{"r":[0.2,0.6]}],["WT.env.r",{"r":[0.05,0.16]}],["WT.env.rep",{"p":[0,0,1,2]}]]})
    return {"name":name,"cat":"XFX","seed":seed,"view":"WT","desc":desc,
      "set":[["layers.WT.oscs.0.table",tables[0]],["layers.WT.oscs.0.level",0.7],["layers.WT.oscs.0.filter.type","moog4pole"],["layers.WT.oscs.0.filter.on",True],["layers.WT.env.a",0.002],["layers.WT.env.d",0.15],["layers.WT.env.s",0.4],["layers.WT.env.r",0.1],["bpm",bpm]],
      "fx":[{"type":"delay","params":[["time",0.1875],["feedback",0.36],["mix",0.2]],"sends":["WT"]},{"type":"glitch","params":[["bitDepth",12],["srReduction",1],["stutterOn",False],["seqOn",True],["seqRate","1/16"],["seqIntensity",0.3],["mix",0.35]],"sends":["WT"]},{"type":"reverb","params":[["algo","plate"],["decay",0.3],["mix",0.12]],"sends":["WT"]}],
      "xfx":{"bank":name,"range":[36,96],"fill":1,"mode":"cycle","palette":pal},
      "clips":{"count":8,"bars":bars,"grid":grid,"density":dens,"leap":leap,"ratchet":ratchet,"pitches":pitches,"names":names,"quant":quant}}
mn=[48,51,53,55,58,60,63,65,67,70,72,75,77,79]

# ------------------------------------------------------------------ Round 20: restored legacy named presets (minus Jungle) + 8 new IDM XFX presets
def kit(r, names, extra_fx=None):
    assert len(names) == 16
    r['set'] = [[k, (names[int(k.split('.')[3])] + ' (synthesized)') if k.endswith('.name') else v] for k, v in r['set']] if isinstance(r['set'], list) else {k: ((names[int(k.split('.')[3])] + ' (synthesized)') if k.endswith('.name') else v) for k, v in r['set'].items()}
    if extra_fx is not None: r['fx'] = extra_fx
    return r
def fxs(drive, bits, delay, rev, seq=False, rate="1/16"):
    return [{"type":"saturation","params":{"drive":drive,"mix":0.4},"sends":["SMP"]},
            {"type":"delay","params":{"time":0.1875,"feedback":0.3,"mix":delay},"sends":["SMP"]},
            {"type":"glitch","params":{"bitDepth":bits,"srReduction":1,"stutterOn":False,"seqOn":seq,"seqRate":rate,"seqIntensity":0.4,"mix":0.45},"sends":["SMP"]},
            {"type":"reverb","params":{"algo":"plate","decay":0.28,"mix":rev},"sends":["SMP"]}]
def lane(n, *hits): return {'len': n, 'steps': pat(*hits, n=n)}
H=[lane(16,0,6,10),lane(16,4,12),lane(16,3,7,11,14,15),lane(16,2,5,9,13),lane(16,*range(0,16,2)),lane(16,1,3,5,7,9,11,13,15),
   lane(16,0,10),lane(16,4,12,15),lane(16,7,14),lane(16,2,3,10,11),lane(16,5,13),lane(16,*range(16)),lane(12,0,3,6,9,n=12) if False else lane(12,0,3,6,9),lane(16,8,9),lane(16,1,6,11),lane(16,12,13,14,15)]
T=[lane(16,0,7,10),lane(16,4,12),lane(16,12),lane(16,6,8,14),lane(16,*range(0,16,2)),lane(32,0,2,3,6,8,11,14,16,18,19,22,24,27,30),
   lane(16,0,1,8),lane(16,4,12),lane(16,13,14),lane(16,4,10,12),lane(16,2,9),lane(16,15),lane(16,7),lane(16,3,11),lane(16,5,13),lane(16,1,9)]
B=[lane(32,0,6,11,16,22,27),lane(32,4,12,20,28,30,31),lane(32,*range(0,32,4)),lane(32,8,24,25,26,27),lane(32,2,5,9,13,18,21,25,29),lane(32,12,13,14,15,28,29,30,31),
   lane(16,0,3,6,9,12,15),lane(32,*range(0,32,2)),lane(32,*range(32)),lane(16,2,4,6,10,12,14),lane(24,0,5,11,17),lane(32,7,15,23,31),lane(32,1,3,19),lane(32,10,26),lane(20,0,8,13),lane(32,14,30,31)]
A=[lane(17,0,5,11),lane(13,3,8),lane(11,0,2,7),lane(19,4,9,14,17),lane(14,1,6,10),lane(9,0,3,6),lane(15,2,12),lane(7,0,3,5),lane(10,1,4,8),lane(21,6,13,18),
   lane(12,0,7),lane(18,2,5,9,16),lane(5,0,2),lane(23,3,11,19),lane(16,8,15),lane(6,1,4)]
M=[lane(24,0,9,15),lane(20,4,13),lane(18,2,8,11,17),lane(14,1,7),lane(12,0,5,9),lane(10,3,6),lane(22,0,3,12,19),lane(7,2,5),lane(15,1,10),lane(9,0,4,7),
   lane(13,2,6,11),lane(11,5,8),lane(8,0,3,6),lane(25,7,16,21),lane(6,1,3),lane(19,0,9,10,15)]
nm=[]
d=drum('Hard IDM Kit XFX',20261120,174,'Hard IDM Kit XFX - XFX MODE with the DRUM SEQUENCER on XFX output. Gabber and hard-techno kicks, industrial snares and clap piles, metal hats and clangs: a heavy, distorted IDM kit at 174 BPM with ghost hits, 16th hat rolls and short repeat-stutter bursts. Keys C2-D#3 play the 16 voices; edit the lanes or play on top.',H,[0,0,1,2,3],0)
nm.append(kit(d,['Gabber Distorted Kick','Hard Techno Rumble Kick','Industrial Kick Slam','Industrial Snare Crack','Breakcore Snare Roll','Noise Snare Crack','Industrial Clap Smear','Layered Clap Pile','Metallic Clap Zap','Industrial Tom Clang','Industrial Hat Hiss','Metallic Hat Ring','Bitcrushed Hat Click','IDM Microglitch Burst','Autechre Metal Zap','Industrial Clang Hit'],fxs(0.55,12,0.14,0.08)))
d=drum('Microglitch Polymeter XFX',20261121,138,'Microglitch Polymeter XFX - XFX MODE drum voices built from clicks, micro-bursts and metal zaps, with 5-23 step lanes that phase against each other at 138 BPM so the pattern keeps evolving. Hats stutter through the glitch sequencer.',A,[0,1,2,4,6],0)
nm.append(kit(d,['IDM Clicky Kick','Gated Kick Punch','Kick Thump Pulse','IDM Glitch Snare','Bitcrushed Snare Snap','Rimshot Pulse Click','Metallic Clap Zap','Metallic Perc Hit','Resonant Perc Ping','Woodblock Click','IDM Microglitch Burst','IDM Glitch Hat','Closed Hat Tick','Glitch Perc Blip','Autechre Metal Zap','Digital Artifact Zap'],fxs(0.25,11,0.2,0.12,True,"1/32")))
d=drum('Trap Phonk IDM XFX',20261122,140,'Trap Phonk IDM XFX - XFX MODE with sliding 808-style kicks, snappy modern snares, phonk cowbell and fast trap hat rolls (32-step hat lane with triplet-feel bursts) at 140 BPM, with IDM-style glitch percussion on the top keys.',T,[0,0,1,2,4],0)
nm.append(kit(d,['808 Sub Kick Long','Hardstyle Reverse Bass Kick','Sub Kick Thud','Modern Pop Snap Snare','Gated Reverb Snare','Bitcrushed Snare Snap','Phonk Cowbell','DnB Tight Clap','Lo-Fi Clap Dust','Metallic Hat Ring','Closed Hat Tick','Open Hat Sizzle','IDM Glitch Hat','Glitch Perc Blip','Resonant Perc Ping','Digital Artifact Zap'],fxs(0.4,13,0.18,0.1)))
d=drum('Breakcore Roll Chops XFX',20261123,192,'Breakcore Roll Chops XFX - XFX MODE with snare-roll heavy 32-step lanes at 192 BPM: gabber kicks under chopped snare rolls, clap stacks and bitcrushed hat bursts, with envelope repeats up to 8 hits per step.',B,[2,3,4,6,8],0)
nm.append(kit(d,['Gabber Distorted Kick','Distorted Kick Crush','Hard Techno Rumble Kick','Breakcore Snare Roll','Industrial Snare Crack','Layered Snare Stack','Layered Clap Pile','Reverse Clap Swell','Metallic Clap Zap','Noise Snare Wall','Bitcrushed Hat Click','IDM Glitch Hat','IDM Perc Stutter','Glitch Stutter Burst','Autechre Metal Zap','IDM Microglitch Burst'],fxs(0.6,10,0.1,0.06,True,"1/32")))
d=drum('Autechre Machine Noise XFX',20261124,112,'Autechre Machine Noise XFX - XFX MODE with cold machine percussion: metal zaps, clangs, noise bursts and pitched ping hits across odd-length lanes at 112 BPM. Slow, lurching and dense; reverb and delay smear the edges.',M,[0,1,2,3,5],0)
nm.append(kit(d,['Metallic Kick Hit','Industrial Kick Slam','Modal Kick','Metallic Snare Ring','Chaos Snare','Noise Snare Wall','Industrial Clang Hit','Metallic Perc Hit','Industrial Tom Clang','Metallic Tom Ring','Autechre Metal Zap','Noise Perc Burst','Resonant Perc Ping','IDM Microglitch Burst','Digital Artifact Zap','Glitch Stutter Burst'],fxs(0.2,12,0.28,0.3)))
nm[-1]['fx'][3]['params']['decay']=0.6
mn2=[45,48,50,52,55,57,60,62,64,67,69,72,74,76]
ld=lead('Hazy Tape Lead XFX',20261125,128,'Hazy Tape Lead XFX - XFX MODE warm, detuned wavetable leads (PWM, ensemble shimmer, Rhodes tines, analog morph) with 8 XFX CLIPS on Bank 1: slow lilting 8th-note IDM melodies at 128 BPM, with a bit-reduced, tape-saturated delay.',['warmPWM','ensembleShimmer','rhodesTine','droneWarmPad','analogMorph','glassHarmonica'],mn2,[0.25,0.5,0.25,0.25],[0.7,0.9],0.18,0.1,[2,2,1,2],['HAZE A','HAZE B','HAZE DRIFT','HAZE LEAP','HAZE SLOW','HAZE ECHO','HAZE CALL','HAZE END'],'1/4',[500,3200])
nm.append(ld)
ld=lead('Neuro Stab Sequence XFX',20261126,174,'Neuro Stab Sequence XFX - XFX MODE with biting neuro and tech-step bass stabs on every key and 8 XFX CLIPS: tight 16th riffs with low leaps and ratchets at 174 BPM. Launch clips on Bank 1; each note picks up a different distorted voice.',['dnbReese','dnbNeuroFold','bassGrowlFangs','bassDistortedSaw','dnbTechStep','bassMetalGrind'],[36,38,39,41,43,44,46,48,50,51,53,55],[0.25,0.25,0.125,0.25],[0.75,0.95],0.15,0.4,[1,1,2,1],['STAB 1','STAB 2','STAB 3','STAB LOW','STAB WALK','STAB RATCHET','STAB CALL','STAB END'],'1/4',[300,2400])
nm.append(ld)
ld=lead('Glitch Pluck Arp XFX',20261127,150,'Glitch Pluck Arp XFX - XFX MODE with short plucks, bells and glass tones (each key a different voice) and 8 XFX CLIPS of evenly rolling 16th and 32nd arpeggios with sparse leaps at 150 BPM. Launch several clips for overlapping IDM arp lines.',['pluckDecay','glassHarmonica','fmLikeBell','crystalShimmer','glitchStutter','quantumFlutter'],[55,58,60,62,65,67,70,72,74,77,79,82],[0.125,0.25,0.125,0.25],[0.65,0.9],0.1,0.25,[1,2,1,1],['PLUCK 1','PLUCK 2','PLUCK 32','PLUCK UP','PLUCK DOWN','PLUCK WEAVE','PLUCK CALL','PLUCK END'],'1/4',[1500,9000])
nm.append(ld)

def extra_named():
    """Legacy named presets lost in the Round 18 rebuild (LUA + SID + XFX kits/leads), without any Jungle presets, then the 8 new IDM ones."""
    legacy = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'named_legacy.json')))
    skip = {'AYFXTWN', 'ENO XFX', 'Glitch XFX'}
    keep = [r for r in legacy if r['name'] not in skip and 'jungle' not in r['name'].lower() and 'junglist' not in r['name'].lower()]
    return keep + nm
