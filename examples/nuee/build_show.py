#!/usr/bin/env python3
"""Build Nuée: the Champ de rupture FM score played by one particle cloud and a moving camera."""
import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess

BASE_DURATION=180.0
# Same landmarks as the score (examples/champ-de-rupture); scaled only when another recording is supplied.
# Each cue: the cloud (amount, pull, trail, exposure, tint) and the camera (path, speed, path size, distance,
# direction). Paths: 0 Fixed, 1 Orbit, 2 Figure eight, 3 Spiral, 4 Pendulum, 5 Fly-through.
COLD,BLUE,VIOLET,WHITE,AMBER=[.55,.72,1],[.4,.6,1],[.62,.5,1],[.9,.95,1],[1,.78,.55]
MOMENTS=[
 (0,"01 · Émergence / poussière lointaine",1.5,dict(amount=.25,pull=.2,trail=.5,exposure=.3,tint=COLD,hot=WHITE,camPath=0,camSpeed=0,camSize=.3,camDistance=.85,camYaw=-.2)),
 (12,"02 · Premier corps / orbite lente",1.1,dict(amount=.4,pull=.3,trail=.55,exposure=.45,tint=COLD,hot=WHITE,camPath=1,camSpeed=.15,camSize=.4,camDistance=.6,camYaw=-.2)),
 (28,"03 · La poussière se souvient",1.8,dict(amount=.5,pull=.35,trail=.7,exposure=.5,tint=BLUE,hot=WHITE,camPath=2,camSpeed=.3,camSize=.5,camDistance=.5,camYaw=0)),
 (44,"04 · Peaux de résonance",1.2,dict(amount=.6,pull=.3,trail=.75,exposure=.5,tint=VIOLET,hot=WHITE,camPath=3,camSpeed=.35,camSize=.6,camDistance=.45,camYaw=.1)),
 (60,"05 · Première chambre / traversée",.15,dict(amount=.7,pull=.1,trail=.6,exposure=.55,tint=BLUE,hot=AMBER,camPath=5,camSpeed=.9,camSize=.9,camDistance=.5,camYaw=.1)),
 (76,"06 · Compression",1.7,dict(amount=.8,pull=.85,trail=.8,exposure=.55,tint=BLUE,hot=WHITE,camPath=1,camSpeed=.5,camSize=.5,camDistance=.3,camYaw=.1)),
 (91,"07 · Grêle métallique",.6,dict(amount=.75,pull=.4,trail=.4,exposure=.6,tint=WHITE,hot=WHITE,camPath=4,camSpeed=.9,camSize=.7,camDistance=.4,camYaw=.3)),
 (104,"08 · Fracture / silence",.12,dict(amount=.5,pull=.2,trail=.93,exposure=.5,tint=BLUE,hot=AMBER,camPath=0,camSpeed=0,camSize=.5,camDistance=.55,camYaw=.4)),
 (116,"09 · Mémoire oblique",2.2,dict(amount=.55,pull=.3,trail=.85,exposure=.5,tint=VIOLET,hot=WHITE,camPath=3,camSpeed=-.4,camSize=.5,camDistance=.5,camYaw=.4)),
 (128,"10 · Masse critique",.8,dict(amount=1,pull=.6,trail=.75,exposure=.5,tint=COLD,hot=AMBER,camPath=3,camSpeed=.6,camSize=.6,camDistance=.45,camYaw=.2)),
 (144,"11 · Le champ cède",.12,dict(amount=.9,pull=0,trail=.7,exposure=.75,tint=WHITE,hot=AMBER,camPath=2,camSpeed=1,camSize=1,camDistance=.5,camYaw=.2)),
 (160,"12 · Débris lents",2.8,dict(amount=.45,pull=.2,trail=.9,exposure=.4,tint=COLD,hot=WHITE,camPath=1,camSpeed=.08,camSize=.4,camDistance=.9,camYaw=0)),
 (172,"13 · Retour au noir",8.,dict(amount=.05,pull=.2,trail=.9,exposure=0,tint=COLD,hot=WHITE,camPath=0,camSpeed=0,camSize=.3,camDistance=1,camYaw=0)),
]
# Slow motions owned by timelines (sine oscillators): name, centre, amplitude, period (s)
MACROS=[("turbulence",.45,.25,47),("camPitch",.2,.25,71),("size",.3,.1,37),("camLens",.45,.15,89)]
# Audio routes owned by interactions: band name, parameter, low Hz, high Hz, feature (0 level, 2 onset)
ROUTES=[("level","energy",20,20000,0),("sub","bass",28,120,0),("metal","metal",700,3200,0),("air","air",6000,16000,0),("onset","hit",20,20000,2)]

def cue_params(values):
    params={}
    for key,value in values.items():
        if key in ("tint","hot"):
            for component,v in zip("rgba",value+[1]):params[f"param/{key}/{component}"]=v
        else:params[f"param/{key}"]=value
    return params

def build(out,audio=None,duration=BASE_DURATION):
    out=Path(out).resolve();out.mkdir(parents=True,exist_ok=True)
    here=Path(__file__).resolve().parent
    (out/"shaders").mkdir(exist_ok=True)
    for name in ("Particles.fs","Particles.vs"):
        nearby=here/"shaders"/name
        shader=nearby if nearby.exists() else here.parents[1]/"isf"/"generators"/name
        destination=out/"shaders"/name
        if shader.resolve()!=destination.resolve():shutil.copy2(shader,destination)
    if audio:
        audio=Path(audio).resolve()
        if not audio.is_file():raise ValueError(f"Audio file not found: {audio}")
        duration=float(subprocess.check_output(["ffprobe","-v","error","-show_entries","format=duration","-of","default=noprint_wrappers=1:nokey=1",str(audio)],text=True))
        (out/"audio").mkdir(exist_ok=True);target=out/"audio"/audio.name
        if audio!=target.resolve():shutil.copy2(audio,target)
        audio_path=f"audio/{audio.name}"
    else:audio_path=None
    ratio=duration/BASE_DURATION
    first=MOMENTS[0][3]
    params={"opacity":0,"blend_mode":"normal","position/x":640,"position/y":360,**cue_params(first)}
    for macro,center,amplitude,period in MACROS:params[f"param/{macro}"]=center
    cloud={"id":"101","name":"Nuée","enable":True,"source":{"type":"isf","path":"shaders/Particles.fs","relative_path":"shaders/Particles.fs","width":1280,"height":720},"params":params,"fx":[],"viewports":{},"color_models":1}
    timelines=[]
    for n,(macro,center,amplitude,period) in enumerate(MACROS,1):
        timelines.append({"id":str(n),"name":f"Nuée · {macro}","duration":period,"loop":"loop","tracks":[{"layer":"101","param":f"param/{macro}","oscillator":{"wave":"sine","period":period,"center":center,"amplitude":amplitude,"phase":n*.17,"seed":n},"keys":[]}]})
    interactions=[]
    for source,param,lo,hi,feature in ROUTES:
        onset=source=="onset"
        interactions.append({"id":str(len(interactions)+1),"name":f"Audio {source} → Nuée","enabled":True,"source":"audio","audio_low":lo,"audio_high":hi,"audio_feature":feature,"layer":"101","param":f"param/{param}","source_range":[0,1 if onset else .55],"target_range":[0,1],"master":1,"filter_on":True,"filter":"attack_release","filter_settings":[.008 if onset else .025,.09 if onset else .24],"takeover":"jump"})
    audio_layer={"id":"102","name":"Nuée · bande sonore","enable":True,"source":{"type":"audio","path":audio_path,"relative_path":audio_path} if audio_path else {"type":"none"},"params":{"play":False,"play_mode":"oneshot","volume":1,"mute":False},"fx":[],"viewports":{}}
    layers=[cloud,audio_layer]
    steps=[{"timeline":timeline["id"],"action":"play","continue":"follow","text":"Armement des mouvements"} for timeline in timelines]
    snapshots=[];cuebook=[]
    for index,(time,name,fade,values) in enumerate(MOMENTS):
        states=copy.deepcopy(layers)
        states[0]["included"]=True
        # Cues own the art direction and the camera; timelines and audio keep their own parameters.
        states[0]["params"]={"opacity":0 if index==len(MOMENTS)-1 else 1,"blend_mode":"normal",**cue_params(values)}
        states[1]["included"]=index==0
        if index==0:states[1]["params"].update({"restart":True,"play":True})
        span=((MOMENTS[index+1][0] if index+1<len(MOMENTS) else BASE_DURATION)-time)*ratio
        fade=min(fade*ratio,span)
        snapshots.append({"id":str(index+1),"name":name,"fade":fade,"layers":states,"composition":{"included":True,"opacity":1,"volume":1},"interactions":[{"id":it["id"],"included":False} for it in interactions]})
        steps.append({"snapshot":str(index+1),"continue":"auto_follow","post_wait":max(0,span-fade),"text":name,"description":f"{time*ratio:.2f} s · {span:.2f} s"})
        cuebook.append({"at":round(time*ratio,3),"duration":round(span,3),"name":name,"fade":round(fade,3),"camera":values["camPath"]})
    for timeline in timelines:steps.append({"timeline":timeline["id"],"action":"stop","continue":"follow","text":"Fin des mouvements"})
    snapshots[-1]["composition"]["volume"]=0
    project={"app":"Pinol","format_version":1,"composition":{"width":1280,"height":720,"fps":60,"speed":1},"audio":{"volume":1},"layers":layers,"interactions":interactions,"timelines":timelines,"snapshots":snapshots,"snapshot_tree":[snap["id"] for snap in snapshots],"sequences":[{"name":"Nuée · particules / 3 minutes","loop":False,"steps":steps}],"current_sequence":0,"startup_sequence":-1,"ui":{"selected_layer":0}}
    (out/"Nuee.pinol").write_text(json.dumps(project,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    (out/"cuebook.json").write_text(json.dumps({"audio":audio_path,"duration":duration,"cues":cuebook},ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    return project,cuebook

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--audio",type=Path);parser.add_argument("--out",type=Path,required=True);parser.add_argument("--duration",type=float,default=BASE_DURATION)
    args=parser.parse_args();project,cues=build(args.out,args.audio,args.duration)
    print(f"Show ready: {args.out.resolve()/'Nuee.pinol'} ({len(project['timelines'])} timelines, {len(cues)} cues)")
