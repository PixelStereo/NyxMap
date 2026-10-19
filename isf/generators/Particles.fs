/*{
 "ISFVSN":"2","DESCRIPTION":"Particles: up to 262 144 grains born by an emitter (sphere, shell, ring, point, line or the light of an image), moved by forces (gravity, wind, drag, turbulence, vortex) and by two attractors (a negative strength repels: it disperses them), drawn with motion streaks, depth of field and bloom, coloured over their life. Their state lives in float textures; Pinol draws them as points (POINTS pass).","CREDIT":"PINOL","CATEGORIES":["Particles","Audio Reactive"],
 "INPUTS":[
 {"NAME":"count","LABEL":"Particles","TYPE":"float","DEFAULT":0.6,"MIN":0,"MAX":1},
 {"NAME":"emitShape","LABEL":"Emitter Shape","TYPE":"long","DEFAULT":1,"VALUES":[0,1,2,3,4,5],"LABELS":["Sphere","Shell","Ring","Point","Line","Image"]},
 {"NAME":"emitPos","LABEL":"Emitter Position","TYPE":"point2D","DEFAULT":[0,0],"MIN":[-1,-1],"MAX":[1,1]},
 {"NAME":"emitSize","LABEL":"Emitter Size","TYPE":"float","DEFAULT":0.5,"MIN":0,"MAX":1},
 {"NAME":"life","LABEL":"Life (s)","TYPE":"float","DEFAULT":6,"MIN":0.2,"MAX":20},
 {"NAME":"lifeRandom","LABEL":"Life Random","TYPE":"float","DEFAULT":0.5,"MIN":0,"MAX":1},
 {"NAME":"emitDir","LABEL":"Emit Direction","TYPE":"long","DEFAULT":0,"VALUES":[0,1,2,3,4],"LABELS":["Outward","Up","Down","Left","Right"]},
 {"NAME":"speed","LABEL":"Initial Speed","TYPE":"float","DEFAULT":0.15,"MIN":0,"MAX":1},
 {"NAME":"spread","LABEL":"Direction Spread","TYPE":"float","DEFAULT":0.5,"MIN":0,"MAX":1},
 {"NAME":"burst","LABEL":"Burst","TYPE":"float","DEFAULT":0,"MIN":0,"MAX":1},
 {"NAME":"timeScale","LABEL":"Time Factor","TYPE":"float","DEFAULT":1,"MIN":0,"MAX":2},
 {"NAME":"gravity","LABEL":"Gravity","TYPE":"float","DEFAULT":0,"MIN":-1,"MAX":1},
 {"NAME":"wind","LABEL":"Wind","TYPE":"point2D","DEFAULT":[0,0],"MIN":[-1,-1],"MAX":[1,1]},
 {"NAME":"drag","LABEL":"Drag","TYPE":"float","DEFAULT":0.35,"MIN":0,"MAX":1},
 {"NAME":"turbulence","LABEL":"Turbulence","TYPE":"float","DEFAULT":0.4,"MIN":0,"MAX":1},
 {"NAME":"turbScale","LABEL":"Turbulence Scale","TYPE":"float","DEFAULT":0.4,"MIN":0,"MAX":1},
 {"NAME":"turbEvolve","LABEL":"Turbulence Evolution","TYPE":"float","DEFAULT":0.2,"MIN":0,"MAX":1},
 {"NAME":"vortex","LABEL":"Vortex","TYPE":"float","DEFAULT":0.15,"MIN":-1,"MAX":1},
 {"NAME":"attr1Pos","LABEL":"Attractor 1 Position","TYPE":"point2D","DEFAULT":[0,0],"MIN":[-1,-1],"MAX":[1,1]},
 {"NAME":"attr1Strength","LABEL":"Attractor 1 Strength","TYPE":"float","DEFAULT":0.35,"MIN":-1,"MAX":1},
 {"NAME":"attr1Radius","LABEL":"Attractor 1 Radius","TYPE":"float","DEFAULT":0.7,"MIN":0,"MAX":1},
 {"NAME":"attr1Swirl","LABEL":"Attractor 1 Swirl","TYPE":"float","DEFAULT":0.3,"MIN":-1,"MAX":1},
 {"NAME":"attr2Pos","LABEL":"Attractor 2 Position","TYPE":"point2D","DEFAULT":[0.55,0.1],"MIN":[-1,-1],"MAX":[1,1]},
 {"NAME":"attr2Strength","LABEL":"Attractor 2 Strength","TYPE":"float","DEFAULT":-0.5,"MIN":-1,"MAX":1},
 {"NAME":"attr2Radius","LABEL":"Attractor 2 Radius","TYPE":"float","DEFAULT":0.3,"MIN":0,"MAX":1},
 {"NAME":"attr2Swirl","LABEL":"Attractor 2 Swirl","TYPE":"float","DEFAULT":0,"MIN":-1,"MAX":1},
 {"NAME":"size","LABEL":"Size","TYPE":"float","DEFAULT":0.3,"MIN":0,"MAX":1},
 {"NAME":"sizeRandom","LABEL":"Size Random","TYPE":"float","DEFAULT":0.6,"MIN":0,"MAX":1},
 {"NAME":"sizeLife","LABEL":"Size over Life","TYPE":"float","DEFAULT":0,"MIN":-1,"MAX":1},
 {"NAME":"colorBirth","LABEL":"Color at Birth","TYPE":"color","DEFAULT":[0.55,0.75,1,1]},
 {"NAME":"colorDeath","LABEL":"Color at Death","TYPE":"color","DEFAULT":[0.6,0.45,1,1]},
 {"NAME":"colorFast","LABEL":"Color when Fast","TYPE":"color","DEFAULT":[1,0.85,0.7,1]},
 {"NAME":"heat","LABEL":"Color by Speed","TYPE":"float","DEFAULT":0.4,"MIN":0,"MAX":1},
 {"NAME":"brightness","LABEL":"Brightness","TYPE":"float","DEFAULT":0.4,"MIN":0,"MAX":1},
 {"NAME":"trail","LABEL":"Trail","TYPE":"float","DEFAULT":0.6,"MIN":0,"MAX":0.98},
 {"NAME":"streaks","LABEL":"Motion Streaks","TYPE":"float","DEFAULT":0.35,"MIN":0,"MAX":1},
 {"NAME":"dof","LABEL":"Depth of Field","TYPE":"float","DEFAULT":0.3,"MIN":0,"MAX":1},
 {"NAME":"bloom","LABEL":"Bloom","TYPE":"float","DEFAULT":0.5,"MIN":0,"MAX":1},
 {"NAME":"sprite","LABEL":"Grain Image","TYPE":"image"},
 {"NAME":"form","LABEL":"Form Image","TYPE":"image"},
 {"NAME":"formPull","LABEL":"Form Hold","TYPE":"float","DEFAULT":0.6,"MIN":0,"MAX":1},
 {"NAME":"camYaw","LABEL":"Camera Direction","TYPE":"float","DEFAULT":0,"MIN":-1,"MAX":1},
 {"NAME":"camPitch","LABEL":"Camera Height","TYPE":"float","DEFAULT":0.25,"MIN":-1,"MAX":1},
 {"NAME":"camDistance","LABEL":"Camera Distance","TYPE":"float","DEFAULT":0.35,"MIN":0,"MAX":1},
 {"NAME":"camTarget","LABEL":"Camera Target","TYPE":"point2D","DEFAULT":[0,0],"MIN":[-1,-1],"MAX":[1,1]},
 {"NAME":"camLens","LABEL":"Camera Lens","TYPE":"float","DEFAULT":0.4,"MIN":0,"MAX":1},
 {"NAME":"camPath","LABEL":"Camera Path","TYPE":"long","DEFAULT":1,"VALUES":[0,1,2,3,4,5],"LABELS":["Fixed","Orbit","Figure eight","Spiral","Pendulum","Fly-through"]},
 {"NAME":"camSpeed","LABEL":"Camera Speed","TYPE":"float","DEFAULT":0.25,"MIN":-1,"MAX":1},
 {"NAME":"camSize","LABEL":"Camera Path Size","TYPE":"float","DEFAULT":0.5,"MIN":0,"MAX":1}
 ],
 "PASSES":[
 {"TARGET":"cam","PERSISTENT":true,"FLOAT":true,"WIDTH":1,"HEIGHT":1},
 {"TARGET":"home","PERSISTENT":true,"FLOAT":true,"WIDTH":512,"HEIGHT":512},
 {"TARGET":"vel","PERSISTENT":true,"FLOAT":true,"WIDTH":512,"HEIGHT":512},
 {"TARGET":"pos","PERSISTENT":true,"FLOAT":true,"WIDTH":512,"HEIGHT":512},
 {"TARGET":"glow","PERSISTENT":true,"FLOAT":true},
 {"TARGET":"glow","PERSISTENT":true,"FLOAT":true,"POINTS":"262144*$count","CLEAR":false},
 {"TARGET":"bloomH","FLOAT":true,"WIDTH":"$WIDTH/4","HEIGHT":"$HEIGHT/4"},
 {"TARGET":"bloomV","FLOAT":true,"WIDTH":"$WIDTH/4","HEIGHT":"$HEIGHT/4"},
 {}
 ]
}*/
// The controls follow the vocabulary of the usual particle engines (Trapcode Particular, TouchDesigner's
// particlesGPU, Houdini POPs, Unity VFX Graph, Niagara):
// - Emitter: its shape, place and size; Life (s) and Life Random; Initial Speed, along Emit Direction (outwards
//   from the shape, or up, down, left, right: a fountain, a rain) and Direction Spread towards any direction; Burst, an outward push from the emitter while it is up. Particles and
//   Life set the birth rate together: Particles / Life grains are born each second.
// - Forces (accelerations, integrated each frame, Time Factor slowing or quickening them all): Gravity (down), Wind
//   (in the picture's plane), Drag (air resistance, proportional to the speed), Turbulence (a curl-noise field: no
//   sinks, the grains swirl without clumping) with its Scale (larger eddies) and Evolution (how fast it changes),
//   Vortex (a spin around the vertical axis through the emitter).
// - Two attractors in the picture's plane: Strength (negative: it repels, a disperser), Radius (its reach, with a
//   soft edge), Swirl (a sideways pull: the grains orbit it instead of falling in).
// - Look: Size, Size Random (mostly fine dust, a few bigger grains), Size over Life (shrinks or grows), its colour
//   from birth to death, warmed towards Color when Fast by its speed, Brightness, Trail, streaks, focus, bloom.
// Pass 0: where the camera is along its path. Pass 1: each grain's home in a Form Image (Emitter Shape: Image).
// Pass 2: velocities (xyz, w: the life it was given). Pass 3: positions (xyz, w: life left, in seconds; 0: born
// again). Passes 1 to 3 decide a birth the same way, from the position before this frame. Pass 4: what was drawn
// fades (the trail). Pass 5: the grains, added as soft streaks (vertex shader: Particles.vs, with the camera).
// Passes 6 and 7: their bloom, a quarter of the size. Pass 8: the light they add up to, brought into the picture.

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
float ptLuma(vec4 c){return dot(c.rgb,vec3(.299,.587,.114))*c.a;}
bool ptFormShape(){return emitShape==5&&_form_imgSize.x>1.;}
// The emitter: its centre and its size, in the space the camera looks at (the cloud about a unit wide)
vec3 ptEmitter(){return vec3(emitPos*1.2,0.);}
float ptEmitterSize(){return mix(.05,1.5,emitSize);}
// Its life: Life, give or take Life Random (at the very start, part of it already spent)
float ptLife(uint id){
 float l=life*(1.+lifeRandom*.9*(ptRand(id*5U+uint(FRAMEINDEX)*7919U)*2.-1.));
 return FRAMEINDEX<2?l*ptRand(id*5U+3U):l;
}
vec3 ptDir(uint s){float z=ptRand(s)*2.-1.,a=ptRand(s+1U)*6.2831853;return vec3(sqrt(1.-z*z)*cos(a),z,sqrt(1.-z*z)*sin(a));}
// Its place in a Form Image, for good: on its light (the brighter, the likelier), as a thin relief
vec3 ptFormPos(uint id){
 uint s=id*5U+1U;
 vec2 best=vec2(.5);float bestL=-1.;
 for(int k=0;k<32;++k){
  vec2 uv=vec2(ptRand(s*13U+uint(k)*2U+1U),ptRand(s*13U+uint(k)*2U+2U));
  float l=ptLuma(texture(form,uv));
  if(l>bestL){best=uv;bestL=l;}
  if(l>ptRand(s*29U+uint(k)+5U)*.9+.05)break;
 }
 float aspect=_form_imgSize.x/_form_imgSize.y;
 vec2 xy=(best-.5)*2.*ptEmitterSize()*vec2(min(aspect,1.6),min(1.,1.6/aspect));
 return ptEmitter()+vec3(xy,(ptRand(s+7U)-.5)*.12);
}
// What identifies the form its places were taken from (the image's size, the emitter's size and place)
float ptFormKey(){return _form_imgSize.x*7.+_form_imgSize.y+emitSize*13.+dot(emitPos,vec2(31.,57.));}
// Where it is born on the emitter, and the way out of it there (the shape's normal)
vec3 ptBirth(uint id,out vec3 n){
 uint s=id*5U+uint(FRAMEINDEX)*7919U;
 vec3 c=ptEmitter(),d=ptDir(s+11U);float R=ptEmitterSize();
 n=d;
 if(ptFormShape()){vec3 p=ptFormPos(id);n=normalize(vec3(p.xy-c.xy,.3)+1e-4);return p;}
 if(emitShape==0)return c+d*R*pow(ptRand(s+2U),1./3.);              // Sphere: its volume
 if(emitShape==2){                                                   // Ring: a flat circle, horizontal
  float a=ptRand(s+2U)*6.2831853;n=vec3(cos(a),0.,sin(a));
  return c+n*R*(1.+(ptRand(s+3U)-.5)*.06)+vec3(0.,(ptRand(s+4U)-.5)*.03,0.);
 }
 if(emitShape==3)return c+d*.01*R;                                   // Point (a speck)
 if(emitShape==4){                                                   // Line: across the picture
  float a=ptRand(s+2U)*6.2831853;n=vec3(0.,cos(a),sin(a));
  return c+vec3((ptRand(s+3U)*2.-1.)*R,0.,0.);
 }
 return c+d*R*(1.+(ptRand(s+3U)-.5)*.08);                            // Shell: its surface
}
// An attractor at a (in the picture's plane): towards it (away when strength < 0) within its reach, fading out
// over the outer half of its reach. Pulling, within its core (a fifth of its reach) the pull weakens with the
// distance, as a spring: the grains gather into a cloud around it instead of falling into one point. Pushing, it is
// strongest at its centre: nothing stays there. Swirl pulls sideways, around the axis facing the camera at rest:
// they orbit it.
vec3 ptAttract(vec3 p,vec2 at,float strength,float radius,float swirl){
 vec3 d=vec3(at*1.2,0.)-p;
 float r=length(d);
 float reach=mix(.1,3.,radius),core=reach*.2;
 float w=(1.-smoothstep(reach*.5,reach,r))*(strength>0.?min(r/core,1.):1.);
 vec3 dir=r>1e-4?d/r:vec3(0,0,-1);
 return w*(strength*6.*dir+swirl*4.*normalize(cross(vec3(0,0,1),dir)+1e-5));
}
// The forces on a grain at p moving at v (accelerations)
vec3 ptForces(vec3 p,vec3 v,uint id,ivec2 c){
 vec3 a=vec3(0.,-4.*gravity,0.)+vec3(wind,0.)*2.;
 float freq=mix(3.5,.4,turbScale);
 a+=ptFlow(p*freq+vec3(0.,TIME*turbEvolve*.5,TIME*turbEvolve*.3))*turbulence*4.;
 vec3 e=p-ptEmitter();
 float rh=length(e.xz);
 a+=vortex*3.*vec3(-e.z,0.,e.x)/max(rh,1e-4)*smoothstep(0.,.25,rh);
 float re=length(e);
 a+=e/max(re,1e-4)*burst*25.;
 a+=ptAttract(p,attr1Pos,attr1Strength,attr1Radius,attr1Swirl);
 a+=ptAttract(p,attr2Pos,attr2Strength,attr2Radius,attr2Swirl);
 // A Form Image holds each grain at its home, a damped spring: the forces shake the form, it comes back
 if(ptFormShape())a+=((texelFetch(home,c,0).xyz-p)*40.-v*8.)*formPull*formPull;
 return a;
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
  float dt=min(TIMEDELTA,.1)*timeScale;
  if(PASSINDEX==1){
   // Its home in the Form Image, taken again when the form changes (and once a second, for an image swapped for
   // another of the same size), so that the whole cloud moves to a new form at once
   vec4 H=texelFetch(home,c,0);
   if(ptFormShape()&&(born||H.w!=ptFormKey()||int(id%60U)==FRAMEINDEX%60))H=vec4(ptFormPos(id),ptFormKey());
   gl_FragColor=H;
  }else if(PASSINDEX==2){
   vec3 n;
   if(born){
    vec3 p=ptBirth(id,n);
    vec3 aim=emitDir==1?vec3(0,1,0):emitDir==2?vec3(0,-1,0):emitDir==3?vec3(-1,0,0):emitDir==4?vec3(1,0,0):n;
    vec3 dir=normalize(mix(aim,ptDir(id*5U+uint(FRAMEINDEX)*7919U+23U),spread)+1e-4);
    gl_FragColor=vec4(dir*speed*3.*(1.-.5*ptRand(id*5U+uint(FRAMEINDEX)*7919U+29U)),ptLife(id));
    return;
   }
   vec3 v=V.xyz+ptForces(P.xyz,V.xyz,id,c)*dt;
   v*=exp(-dt*mix(.05,4.,drag));                        // drag: resistance in proportion to the speed
   float sp=length(v);if(sp>6.)v*=6./sp;
   gl_FragColor=vec4(v,V.w);
  }else{
   vec3 n;
   if(born){gl_FragColor=vec4(ptBirth(id,n),ptLife(id));return;}
   vec3 p=P.xyz+V.xyz*dt;                                // with this frame's velocity (pass 2)
   float left=P.w-dt;
   if(length(p-ptEmitter())>10.)left=0.;                 // gone too far: born again at the next frame
   gl_FragColor=vec4(p,max(left,0.));
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
  g*=.6+6.*brightness*brightness;
  // Filmic tone curve (ACES fit): deep blacks, highlights that roll off instead of clipping
  vec3 c=clamp((g*(2.51*g+.03))/(g*(2.43*g+.59)+.14),0.,1.);
  vec2 v=uv-.5;
  c*=1.-.45*dot(v,v);
  c+=(ptRand(uint(gl_FragCoord.x)*1973U^uint(gl_FragCoord.y)*9277U^uint(FRAMEINDEX)*26699U)-.5)*.012; // grain
  gl_FragColor=vec4(max(c,0.),1.);
 }
}
