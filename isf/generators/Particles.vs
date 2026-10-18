// Particles.fs: the quad of every pass, except the particles' pass (5), where each particle (isf_PointIndex() → its
// texel in the state textures) is a small streak seen by the camera: along its motion on the picture, blurred out of
// the plane in focus, some grains bigger and brighter than the rest.
varying vec4 pColor;
varying vec2 pStreak;

uint ptHashV(uint x){x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;x^=x>>16;return x;}
float ptRandV(uint x){return float(ptHashV(x))*(1.0/4294967295.0);}

// Where the camera is on a path (Fixed, Orbit, Figure eight, Spiral, Pendulum, Fly-through) after c cycles, around
// its target: its direction, its height (radians) and its distance, moved by the path
vec3 ptEye(int path,float c){
 const float TAU=6.2831853;
 float a=camSize,yaw=camYaw*3.1415927,pitch=camPitch*1.3,dist=mix(1.4,7.,camDistance);
 if(path==1){yaw+=TAU*c;}
 else if(path==2){yaw+=a*1.4*sin(TAU*c);pitch+=a*.6*sin(2.*TAU*c);}
 else if(path==3){yaw+=TAU*c;pitch+=a*1.1*sin(TAU*c*.25);dist*=1.+.45*a*sin(TAU*c*.5);}
 else if(path==4){yaw+=a*1.6*sin(TAU*c);}
 else if(path==5){yaw+=a*.8*sin(TAU*c*.5);dist=mix(dist,.25,a*(.5-.5*cos(TAU*c)));}
 return vec3(yaw,clamp(pitch,-1.45,1.45),dist);
}

// The camera (from the "cam" pass: cycles travelled, the path it leaves, the one it goes to, how far it is): its
// place, and its axes right, up and forward, looking at its target
vec3 ptCamera(out vec3 right, out vec3 up, out vec3 forward){
 vec4 C=texelFetch(cam,ivec2(0),0);
 // From one path to the other along the sphere (the shorter way round), not through it
 vec3 a=ptEye(int(C.y+.5),C.x),b=ptEye(int(C.z+.5),C.x);
 float t=smoothstep(0.,1.,C.w);
 float yaw=a.x+(mod(b.x-a.x+3.1415927,6.2831853)-3.1415927)*t,pitch=mix(a.y,b.y,t),dist=mix(a.z,b.z,t);
 vec3 target=vec3(camTarget,0.);
 vec3 eye=target+dist*vec3(cos(pitch)*sin(yaw),sin(pitch),cos(pitch)*cos(yaw));
 forward=normalize(target-eye);
 right=normalize(cross(forward,vec3(0,1,0)));
 up=cross(right,forward);
 return eye;
}

// A point seen by the camera: where on the picture (normalized device coordinates) and how far in front
vec3 ptProject(vec3 p,vec3 eye,vec3 right,vec3 up,vec3 forward,float focal){
 vec3 d=p-eye;
 float depth=dot(d,forward);
 vec2 xy=vec2(dot(d,right),dot(d,up))*focal/max(depth,.05);
 xy.x*=RENDERSIZE.y/RENDERSIZE.x;
 return vec3(xy,depth);
}

void main(){
 if(PASSINDEX!=5){isf_vertShaderInit();return;}
 int id=isf_PointIndex();
 ivec2 c=ivec2(id%512,id/512);
 vec4 P=texelFetch(pos,c,0),V=texelFetch(vel,c,0);
 vec3 right,up,forward;
 vec3 eye=ptCamera(right,up,forward);
 float focal=mix(1.,4.,camLens);
 // Its head now and its tail a moment ago: the streak
 vec3 head=ptProject(P.xyz,eye,right,up,forward,focal);
 vec3 tail=ptProject(P.xyz-V.xyz*(.01+.07*streaks)*(.45+.8*energy),eye,right,up,forward,focal);
 // Its own size and brightness, for good: mostly fine dust, a few bright grains
 uint u=uint(id);
 float big=pow(ptRandV(u*3U+1U),8.),shine=pow(ptRandV(u*3U+2U),4.);
 float sizeVar=mix(1.,.55+2.6*big,variety)*(_sprite_imgSize.x>1.?2.5:1.),brightVar=mix(1.,.45+1.7*shine,variety);
 // Out of focus away from the camera's target: bigger and dimmer, the same light spread out
 float focus=length(vec3(camTarget,0.)-eye);
 float blur=clamp(abs(head.z-focus)/focus*dof*2.5,0.,1.5);
 float spread=1.+5.*blur;
 float k=clamp(3.2/head.z,.4,2.); // nearer: bigger, brighter; very near: fading out of the way
 float px=clamp((1.+8.*size)*RENDERSIZE.y/720.*k*focal/2.2*sizeVar,1.,(_sprite_imgSize.x>1.?64.:24.)*RENDERSIZE.y/720.)*spread;
 // The square along its motion on the picture (pixels), as long as the streak
 vec2 halfSize=.5*RENDERSIZE;
 vec2 pa=head.xy*halfSize,pb=tail.xy*halfSize,dir=pa-pb;
 float len=min(length(dir),28.*RENDERSIZE.y/720.);
 vec2 ax=len>1e-3?normalize(dir):vec2(1,0),ay=vec2(-ax.y,ax.x);
 float hw=px*.5,hl=hw+len*.5;
 const vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(-1,1),vec2(1,-1),vec2(1,1));
 vec2 kq=corners[gl_VertexID%6];
 vec2 at=(pa+pb)*.5+ax*kq.x*hl+ay*kq.y*hw;
 isf_PointCoord=kq*.5+.5;
 // A particle waiting for its birth, or behind the camera, is out of the picture
 gl_Position=P.w>0.&&head.z>.05&&tail.z>.05?vec4(at/halfSize,0,1):vec4(2,2,0,1);
 pStreak=vec2(hl/hw,blur);
 float age=V.w-P.w;
 float fade=smoothstep(0.,.8,age)*smoothstep(0.,1.2,P.w)*smoothstep(.15,.6,head.z);
 float speed=length(V.xyz);
 // Its colour: from Tint to Hot with its speed and the hits, and across the cloud (a slow gradient in space)
 float across=.5+.5*sin(dot(P.xyz,vec3(1.3,2.1,-.7))*1.6+TIME*.05);
 vec3 col=mix(tint.rgb,hot.rgb,clamp(speed*.45+hit*.8+(across-.5)*.6+(ptRandV(u*3U+3U)-.5)*.25,0.,1.));
 // The far side of the cloud quieter than the near one: its volume reads
 fade*=mix(1.2,.45,smoothstep(focus-.9,focus+.9,head.z));
 // Its light over the frames it stays (the trail keeps 1/(1 - trail) of it): a longer trail, not a brighter cloud;
 // spread over its streak and its blur, not added by them
 pColor=vec4(col,fade*k*(.02+.05*energy)/(.4+amount)*(1.-trail)*2.5*brightVar/(spread*spread)/(hl/hw)*sizeVar*(_sprite_imgSize.x>1.?.16:1.));
}
