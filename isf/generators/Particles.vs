// Particles.fs: the quad of every pass, except the particles' pass (3), where each vertex is one particle read from
// the state textures (gl_VertexID → its texel) and placed by a slowly turning perspective camera.
varying vec4 pColor;

void main(){
 if(PASSINDEX!=3){isf_vertShaderInit();return;}
 ivec2 c=ivec2(gl_VertexID%512,gl_VertexID/512);
 vec4 P=texelFetch(pos,c,0),V=texelFetch(vel,c,0);
 float a=TIME*orbit*.35,ca=cos(a),sa=sin(a);
 vec3 p=vec3(ca*P.x+sa*P.z,P.y,-sa*P.x+ca*P.z);
 float tilt=.35,ct=cos(tilt),st=sin(tilt);
 p=vec3(p.x,ct*p.y-st*p.z,st*p.y+ct*p.z);
 float depth=max(3.2-p.z,.2);
 vec2 xy=p.xy*2.2/depth;
 xy.x*=RENDERSIZE.y/RENDERSIZE.x;
 gl_Position=P.w>0.?vec4(xy,0,1):vec4(2,2,0,1); // a particle waiting for its birth is out of the picture
 float k=clamp(3.2/depth,.4,2.); // nearer: bigger, brighter; very near: fading out of the way
 gl_PointSize=clamp((1.+8.*size)*RENDERSIZE.y/720.*k,1.,16.*RENDERSIZE.y/720.);
 float age=V.w-P.w;
 float fade=smoothstep(0.,.8,age)*smoothstep(0.,1.2,P.w)*smoothstep(.8,1.6,depth);
 float speed=length(V.xyz);
 vec3 col=mix(tint.rgb,hot.rgb,clamp(speed*.5+hit*.8,0.,1.));
 pColor=vec4(col,fade*k*(.02+.05*energy)/(.4+amount));
}
