/*{
 "ISFVSN":"2","DESCRIPTION":"Particles: up to 262 144 grains following a curl-noise flow around a shell, blown outwards by hits, drawn with motion streaks, depth of field and bloom. An image can give each grain its shape (Grain Image) or the cloud its shape (Form Image). Their state lives in float textures; Pinol draws them as points (POINTS pass).","CREDIT":"PINOL","CATEGORIES":["Particles","Audio Reactive"],
 "INPUTS":[
 {"NAME":"amount","LABEL":"Amount","TYPE":"float","DEFAULT":0.6,"MIN":0,"MAX":1},
 {"NAME":"energy","LABEL":"Energy","TYPE":"float","DEFAULT":0.3,"MIN":0,"MAX":1},
 {"NAME":"bass","LABEL":"Bass","TYPE":"float","DEFAULT":0.2,"MIN":0,"MAX":1},
 {"NAME":"metal","LABEL":"Metal","TYPE":"float","DEFAULT":0.2,"MIN":0,"MAX":1},
 {"NAME":"air","LABEL":"Air","TYPE":"float","DEFAULT":0.2,"MIN":0,"MAX":1},
 {"NAME":"hit","LABEL":"Hit","TYPE":"float","DEFAULT":0,"MIN":0,"MAX":1},
 {"NAME":"turbulence","LABEL":"Turbulence","TYPE":"float","DEFAULT":0.4,"MIN":0,"MAX":1},
 {"NAME":"pull","LABEL":"Pull","TYPE":"float","DEFAULT":0.3,"MIN":0,"MAX":1},
 {"NAME":"size","LABEL":"Size","TYPE":"float","DEFAULT":0.3,"MIN":0,"MAX":1},
 {"NAME":"exposure","LABEL":"Exposure","TYPE":"float","DEFAULT":0.4,"MIN":0,"MAX":1},
 {"NAME":"trail","LABEL":"Trail","TYPE":"float","DEFAULT":0.6,"MIN":0,"MAX":0.98},
 {"NAME":"tint","LABEL":"Tint","TYPE":"color","DEFAULT":[0.55,0.75,1,1]},
 {"NAME":"hot","LABEL":"Hot","TYPE":"color","DEFAULT":[1,0.85,0.7,1]},
 {"NAME":"camYaw","LABEL":"Camera Direction","TYPE":"float","DEFAULT":0,"MIN":-1,"MAX":1},
 {"NAME":"camPitch","LABEL":"Camera Height","TYPE":"float","DEFAULT":0.25,"MIN":-1,"MAX":1},
 {"NAME":"camDistance","LABEL":"Camera Distance","TYPE":"float","DEFAULT":0.35,"MIN":0,"MAX":1},
 {"NAME":"camTarget","LABEL":"Camera Target","TYPE":"point2D","DEFAULT":[0,0],"MIN":[-1,-1],"MAX":[1,1]},
 {"NAME":"camLens","LABEL":"Camera Lens","TYPE":"float","DEFAULT":0.4,"MIN":0,"MAX":1},
 {"NAME":"camPath","LABEL":"Camera Path","TYPE":"long","DEFAULT":1,"VALUES":[0,1,2,3,4,5],"LABELS":["Fixed","Orbit","Figure eight","Spiral","Pendulum","Fly-through"]},
 {"NAME":"camSpeed","LABEL":"Camera Speed","TYPE":"float","DEFAULT":0.25,"MIN":-1,"MAX":1},
 {"NAME":"camSize","LABEL":"Camera Path Size","TYPE":"float","DEFAULT":0.5,"MIN":0,"MAX":1},
 {"NAME":"streaks","LABEL":"Motion Streaks","TYPE":"float","DEFAULT":0.35,"MIN":0,"MAX":1},
 {"NAME":"dof","LABEL":"Depth of Field","TYPE":"float","DEFAULT":0.3,"MIN":0,"MAX":1},
 {"NAME":"bloom","LABEL":"Bloom","TYPE":"float","DEFAULT":0.5,"MIN":0,"MAX":1},
 {"NAME":"variety","LABEL":"Grain Variety","TYPE":"float","DEFAULT":0.6,"MIN":0,"MAX":1},
 {"NAME":"sprite","LABEL":"Grain Image","TYPE":"image"},
 {"NAME":"form","LABEL":"Form Image","TYPE":"image"},
 {"NAME":"formPull","LABEL":"Form Pull","TYPE":"float","DEFAULT":0.6,"MIN":0,"MAX":1},
 {"NAME":"formSize","LABEL":"Form Size","TYPE":"float","DEFAULT":0.5,"MIN":0,"MAX":1}
 ],
 "PASSES":[
 {"TARGET":"cam","PERSISTENT":true,"FLOAT":true,"WIDTH":1,"HEIGHT":1},
 {"TARGET":"home","PERSISTENT":true,"FLOAT":true,"WIDTH":512,"HEIGHT":512},
 {"TARGET":"vel","PERSISTENT":true,"FLOAT":true,"WIDTH":512,"HEIGHT":512},
 {"TARGET":"pos","PERSISTENT":true,"FLOAT":true,"WIDTH":512,"HEIGHT":512},
 {"TARGET":"glow","PERSISTENT":true,"FLOAT":true},
 {"TARGET":"glow","PERSISTENT":true,"FLOAT":true,"POINTS":"262144*$amount","CLEAR":false},
 {"TARGET":"bloomH","FLOAT":true,"WIDTH":"$WIDTH/4","HEIGHT":"$HEIGHT/4"},
 {"TARGET":"bloomV","FLOAT":true,"WIDTH":"$WIDTH/4","HEIGHT":"$HEIGHT/4"},
 {}
 ]
}*/
// Pass 0: where the camera is along its path (its speed changes it smoothly, a new path is reached gradually).
// Pass 1: each particle's home, its place in a Form Image: what pulls it back. Pass 2: velocities (xyz, w: its
// life when it was born). Pass 3: positions (xyz, w: life left, in seconds; 0: born again). Passes 1 to 3 decide a
// birth the same way, from the position before this frame. Pass 4: what was drawn fades (the trail). Pass 5: the
// particles, added as soft streaks (vertex shader: Particles.vs, with the camera). Passes 6 and 7: their bloom, a
// quarter of the size. Pass 8: the light they add up to, brought into the picture.

varying vec4 pColor;
varying vec2 pStreak; // pass 5: how long its streak is (its length over its width) and how out of focus it is

uint ptHash(uint x){x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;x^=x>>16;return x;}
float ptRand(uint x){return float(ptHash(x))*(1.0/4294967295.0);}
float ptLattice(ivec3 i){return ptRand(uint(i.x)*73856093U^uint(i.y)*19349663U^uint(i.z)*83492791U);}
// Value noise and its gradient (x: value, yzw: gradient), from the 8 corners of its cell
vec4 ptNoise(vec3 p){
 ivec3 i=ivec3(floor(p));vec3 f=fract(p);
 vec3 u=f*f*f*(f*(f*6.-15.)+10.),du=30.*f*f*(f*(f-2.)+1.);
 float a=ptLattice(i),b=ptLattice(i+ivec3(1,0,0)),c=ptLattice(i+ivec3(0,1,0)),d=ptLattice(i+ivec3(1,1,0));
 float e=ptLattice(i+ivec3(0,0,1)),g=ptLattice(i+ivec3(1,0,1)),h=ptLattice(i+ivec3(0,1,1)),k=ptLattice(i+ivec3(1,1,1));
 float k1=b-a,k2=c-a,k3=e-a,k4=a-b-c+d,k5=a-c-e+h,k6=a-b-e+g,k7=-a+b+c-d+e-g-h+k;
 return vec4(a-.5+k1*u.x+k2*u.y+k3*u.z+k4*u.x*u.y+k5*u.y*u.z+k6*u.z*u.x+k7*u.x*u.y*u.z,
  du*vec3(k1+k4*u.y+k6*u.z+k7*u.y*u.z,k2+k5*u.z+k4*u.x+k7*u.z*u.x,k3+k6*u.x+k5*u.y+k7*u.x*u.y));
}
// A flow without sources or sinks (the curl of three noises): the grains swirl without clumping
vec3 ptCurl(vec3 p){
 vec3 gx=ptNoise(p).yzw,gy=ptNoise(p+vec3(31.4,7.1,-12.9)).yzw,gz=ptNoise(p+vec3(-5.3,47.2,18.8)).yzw;
 return vec3(gz.y-gy.z,gx.z-gz.x,gy.x-gx.y);
}

// A curl at two scales: large swirls, and finer filaments riding them
vec3 ptFlow(vec3 p){return ptCurl(p)+.45*ptCurl(p*2.3+vec3(17.1,-3.7,9.2));}

uint ptId(){ivec2 c=ivec2(gl_FragCoord.xy);return uint(c.y*512+c.x);}
// Its birth: a life of 2 to 9 s (at the very start, part of it already spent)
float ptLife(uint id){
 float l=mix(2.,9.,ptRand(id*4U+uint(FRAMEINDEX)*7919U));
 if(_form_imgSize.x>1.)l*=1.6; // a form holds its grains longer
 return FRAMEINDEX<2?l*ptRand(id*4U+3U):l;
}
float ptLuma(vec4 c){return dot(c.rgb,vec3(.299,.587,.114))*c.a;}
// Its place in a Form Image, for good: on its light (the brighter, the likelier), as a thin relief
vec3 ptFormPos(uint id){
 uint s=id*4U+1U;
 vec2 best=vec2(.5);float bestL=-1.;
 for(int k=0;k<32;++k){
  vec2 uv=vec2(ptRand(s*13U+uint(k)*2U+1U),ptRand(s*13U+uint(k)*2U+2U));
  float l=ptLuma(texture(form,uv));
  if(l>bestL){best=uv;bestL=l;}
  if(l>ptRand(s*29U+uint(k)+5U)*.9+.05)break;
 }
 float aspect=_form_imgSize.x/_form_imgSize.y;
 vec2 xy=(best-.5)*2.*mix(.5,2.,formSize)*vec2(min(aspect,1.6),min(1.,1.6/aspect));
 return vec3(xy,(ptRand(s+7U)-.5)*.12);
}
// What identifies the form its places were taken from (the image's size and the Form Size)
float ptFormKey(){return _form_imgSize.x*7.+_form_imgSize.y+formSize*13.;}
// Where it is born: at its place in a Form Image, or on the shell the flow holds the cloud to (a little inside or
// outside it)
vec3 ptBirthPos(uint id){
 if(_form_imgSize.x>1.)return ptFormPos(id);
 uint s=id*4U+uint(FRAMEINDEX)*7919U;
 float z=ptRand(s+1U)*2.-1.,a=ptRand(s+2U)*6.2831853,r=.75*(1.+(ptRand(s+3U)-.5)*.25);
 return r*vec3(sqrt(1.-z*z)*cos(a),z,sqrt(1.-z*z)*sin(a));
}

void main(){
 if(PASSINDEX==0){
  // x: cycles travelled (one in 20 s at full speed; 4 bring every path back to its start). A new path is
  // reached in 2 s from where the camera is: y the path it leaves, z the one it goes to, w how far it is.
  vec4 C=texelFetch(cam,ivec2(0),0);
  float dt=min(TIMEDELTA,.1);
  if(FRAMEINDEX==0)C=vec4(0,camPath,camPath,1);
  if(float(camPath)!=C.z)C=vec4(C.x,C.w<.5?C.y:C.z,camPath,0);
  gl_FragColor=vec4(mod(C.x+dt*camSpeed*.05,4.),C.y,C.z,min(C.w+dt*.5,1.));
 }else if(PASSINDEX<4){
  uint id=ptId();ivec2 c=ivec2(gl_FragCoord.xy);
  vec4 P=texelFetch(pos,c,0),V=texelFetch(vel,c,0);
  bool born=P.w<=0.;
  float dt=min(TIMEDELTA,.1);
  if(PASSINDEX==1){
   // With a Form Image: its place there, taken again when the form changes (and once a second, for an image swapped
   // for another of the same size), so that the whole cloud moves to a new form at once
   vec4 H=texelFetch(home,c,0);
   if(_form_imgSize.x>1.){
    if(born||H.w!=ptFormKey()||int(id%60U)==FRAMEINDEX%60)H=vec4(ptFormPos(id),ptFormKey());
    gl_FragColor=H;
   }else gl_FragColor=born?vec4(ptBirthPos(id),0):H;
  }else if(PASSINDEX==2){
   if(born){vec3 p=ptBirthPos(id);gl_FragColor=vec4(cross(p,vec3(0,1,0))*.15,ptLife(id));return;}
   // The grains follow the flow's lines (their velocity eases towards it: some inertia for the hits), which gathers
   // them into sheets and filaments; a shell of radius R holds the cloud (the bass draws it in), Pull how firmly
   vec3 flow=ptFlow(P.xyz*(.9+1.8*metal)+vec3(0,TIME*.06,0))*(.25+.9*turbulence+1.2*air);
   float R=.75*(1.-.35*bass*pull),r=length(P.xyz);
   vec3 shell=P.xyz/max(r,1e-4)*(R-r)*(.4+2.6*pull);
   vec3 burst=P.xyz/max(r,1e-4)*hit*2.5;
   vec3 want=flow+shell;
   // A Form Image: back home, firmly (the sound shakes the form, the form comes back)
   if(_form_imgSize.x>1.)want=flow*(.12+.88*(1.-formPull)*(1.-formPull))+(texelFetch(home,c,0).xyz-P.xyz)*formPull*6.;
   vec3 v=mix(V.xyz,want,1.-exp(-dt*(3.+3.*energy)))+burst*dt*4.;
   gl_FragColor=vec4(v,V.w);
  }else{
   if(born){gl_FragColor=vec4(ptBirthPos(id),ptLife(id));return;}
   vec3 p=P.xyz+V.xyz*dt*(.45+.8*energy);
   float life=P.w-dt;
   if(dot(p,p)>6.)life=0.; // gone too far: born again at the next frame
   gl_FragColor=vec4(p,max(life,0.));
  }
 }else if(PASSINDEX==4){
  gl_FragColor=texelFetch(glow,ivec2(gl_FragCoord.xy),0)*trail;
 }else if(PASSINDEX==5){
  // A capsule along its motion: a bright core and a soft halo when in focus, a disc with a brighter rim
  // (bokeh) when not; or the Grain Image
  vec2 q=isf_PointCoord*2.-1.;
  float ratio=max(pStreak.x,1.),blur=clamp(pStreak.y,0.,1.);
  vec2 s=vec2(q.x*ratio,q.y);
  float d=length(vec2(max(abs(s.x)-(ratio-1.),0.),s.y));
  float sharp=exp(-d*d*10.)+.22*exp(-d*d*2.5);
  float bokeh=smoothstep(1.,.82,d)*(.55+.45*smoothstep(.35,.95,d));
  float a=mix(sharp,bokeh*.6,blur)*step(d,1.);
  if(_sprite_imgSize.x>1.)a=ptLuma(texture(sprite,isf_PointCoord))*mix(1.,.6,blur);
  gl_FragColor=vec4(pColor.rgb*pColor.a*a,pColor.a*a);
 }else if(PASSINDEX<8){
  // A wide gaussian, one direction per pass, on a quarter-size picture
  vec2 dir=PASSINDEX==6?vec2(1./RENDERSIZE.x,0):vec2(0,1./RENDERSIZE.y);
  vec3 sum=vec3(0);float total=0.;
  for(int i=-6;i<=6;++i){
   float w=exp(-float(i*i)/18.);
   vec2 uv=isf_FragNormCoord+dir*float(i)*1.3;
   sum+=w*(PASSINDEX==6?texture(glow,uv).rgb:texture(bloomH,uv).rgb);
   total+=w;
  }
  gl_FragColor=vec4(sum/total,1.);
 }else{
  vec2 uv=isf_FragNormCoord;
  vec3 g=texelFetch(glow,ivec2(gl_FragCoord.xy),0).rgb+texture(bloomV,uv).rgb*bloom*1.6;
  g*=.6+6.*exposure*exposure;
  // Filmic tone curve (ACES fit): deep blacks, highlights that roll off instead of clipping
  vec3 c=clamp((g*(2.51*g+.03))/(g*(2.43*g+.59)+.14),0.,1.);
  vec2 v=uv-.5;
  c*=1.-.45*dot(v,v);
  c+=(ptRand(uint(gl_FragCoord.x)*1973U^uint(gl_FragCoord.y)*9277U^uint(FRAMEINDEX)*26699U)-.5)*.012; // grain
  gl_FragColor=vec4(max(c,0.),1.);
 }
}
