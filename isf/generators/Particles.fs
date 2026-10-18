/*{
 "ISFVSN":"2","DESCRIPTION":"Particles: up to 262 144 grains carried by a curl-noise flow, pulled towards the centre and blown outwards by hits. Their state lives in two float textures; Pinol draws them as points (POINTS pass).","CREDIT":"PINOL","CATEGORIES":["Particles","Audio Reactive"],
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
 {"NAME":"exposure","LABEL":"Exposure","TYPE":"float","DEFAULT":0.5,"MIN":0,"MAX":1},
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
 {"NAME":"camSize","LABEL":"Camera Path Size","TYPE":"float","DEFAULT":0.5,"MIN":0,"MAX":1}
 ],
 "PASSES":[
 {"TARGET":"cam","PERSISTENT":true,"FLOAT":true,"WIDTH":1,"HEIGHT":1},
 {"TARGET":"vel","PERSISTENT":true,"FLOAT":true,"WIDTH":512,"HEIGHT":512},
 {"TARGET":"pos","PERSISTENT":true,"FLOAT":true,"WIDTH":512,"HEIGHT":512},
 {"TARGET":"glow","PERSISTENT":true,"FLOAT":true},
 {"TARGET":"glow","PERSISTENT":true,"FLOAT":true,"POINTS":"262144*$amount","CLEAR":false},
 {}
 ]
}*/
// Pass 0: where the camera is along its path (its speed changes it smoothly, a new path is reached gradually).
// Pass 1: velocities (xyz, w: the particle's life when it was born). Pass 2: positions (xyz, w: life left, in
// seconds; 0: born again). Both decide a birth the same way, from the position before this frame.
// Pass 3: what was drawn fades (the trail). Pass 4: the particles, added as soft points (vertex shader:
// Particles.vs, with the camera). Pass 5: the light they add up to, brought into the picture.

varying vec4 pColor;

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

uint ptId(){ivec2 c=ivec2(gl_FragCoord.xy);return uint(c.y*512+c.x);}
// Its birth: a place in a sphere, a slow swirl, a life of 2 to 9 s (at the very start, part of it already spent)
float ptLife(uint id){
 float l=mix(2.,9.,ptRand(id*4U+uint(FRAMEINDEX)*7919U));
 return FRAMEINDEX<2?l*ptRand(id*4U+3U):l;
}
vec3 ptBirthPos(uint id){
 uint s=id*4U+uint(FRAMEINDEX)*7919U;
 float z=ptRand(s+1U)*2.-1.,a=ptRand(s+2U)*6.2831853,r=.9*pow(ptRand(s+3U),.333);
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
 }else if(PASSINDEX<3){
  uint id=ptId();ivec2 c=ivec2(gl_FragCoord.xy);
  vec4 P=texelFetch(pos,c,0),V=texelFetch(vel,c,0);
  bool born=P.w<=0.;
  float dt=min(TIMEDELTA,.1);
  if(PASSINDEX==1){
   if(born){vec3 p=ptBirthPos(id);gl_FragColor=vec4(cross(p,vec3(0,1,0))*.15,ptLife(id));return;}
   vec3 flow=ptCurl(P.xyz*(1.1+2.2*metal)+vec3(0,TIME*.07,0))*(.35+1.2*turbulence+1.6*air);
   vec3 toward=-P.xyz*(.4+1.6*bass)*pull*2.;
   vec3 burst=normalize(P.xyz+1e-4)*hit*5.;
   vec3 v=V.xyz+(flow+toward+burst)*dt;
   v*=exp(-dt*(1.4-.8*energy));
   gl_FragColor=vec4(v,V.w);
  }else{
   if(born){gl_FragColor=vec4(ptBirthPos(id),ptLife(id));return;}
   vec3 p=P.xyz+V.xyz*dt*(.45+.8*energy);
   float life=P.w-dt;
   if(dot(p,p)>6.)life=0.; // gone too far: born again at the next frame
   gl_FragColor=vec4(p,max(life,0.));
  }
 }else if(PASSINDEX==3){
  gl_FragColor=texelFetch(glow,ivec2(gl_FragCoord.xy),0)*trail;
 }else if(PASSINDEX==4){
  vec2 d=gl_PointCoord*2.-1.;
  float a=exp(-dot(d,d)*3.5)*step(dot(d,d),1.);
  gl_FragColor=vec4(pColor.rgb*pColor.a*a,pColor.a*a);
 }else{
  vec3 g=texelFetch(glow,ivec2(gl_FragCoord.xy),0).rgb*(.6+6.*exposure*exposure);
  vec3 c=1.-exp(-g);
  vec2 uv=isf_FragNormCoord-.5;
  c*=1.-.35*dot(uv,uv);
  gl_FragColor=vec4(c,1.);
 }
}
