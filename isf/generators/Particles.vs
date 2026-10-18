// Particles.fs: the quad of every pass, except the particles' pass (4), where each particle (isf_PointIndex() → its
// texel in the state textures) is a small square seen by the camera.
varying vec4 pColor;

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

void main(){
 if(PASSINDEX!=4){isf_vertShaderInit();return;}
 int id=isf_PointIndex();
 ivec2 c=ivec2(id%512,id/512);
 vec4 P=texelFetch(pos,c,0),V=texelFetch(vel,c,0);
 vec3 right,up,forward;
 vec3 d=P.xyz-ptCamera(right,up,forward);
 float depth=dot(d,forward);
 float focal=mix(1.,4.,camLens);
 vec2 xy=vec2(dot(d,right),dot(d,up))*focal/max(depth,.05);
 xy.x*=RENDERSIZE.y/RENDERSIZE.x;
 // A particle waiting for its birth, or behind the camera, is out of the picture
 float k=clamp(3.2/depth,.4,2.); // nearer: bigger, brighter; very near: fading out of the way
 float px=clamp((1.+8.*size)*RENDERSIZE.y/720.*k*focal/2.2,1.,16.*RENDERSIZE.y/720.);
 gl_Position=isf_PointSprite(P.w>0.&&depth>.05?vec4(xy,0,1):vec4(2,2,0,1),px);
 float age=V.w-P.w;
 float fade=smoothstep(0.,.8,age)*smoothstep(0.,1.2,P.w)*smoothstep(.15,.6,depth);
 float speed=length(V.xyz);
 vec3 col=mix(tint.rgb,hot.rgb,clamp(speed*.5+hit*.8,0.,1.));
 // Its light over the frames it stays (the trail keeps 1/(1 - trail) of it): a longer trail, not a brighter cloud
 pColor=vec4(col,fade*k*(.02+.05*energy)/(.4+amount)*(1.-trail)*2.5);
}
