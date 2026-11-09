/*{
  "ISFVSN":"2",
  "DESCRIPTION":"Camera Inversa: perspective, volumetric extinction and RGB frame memory.",
  "CATEGORIES":["Generator","3D"],
  "INPUTS":[
    {"NAME":"smoke","TYPE":"float","MIN":0,"MAX":1,"DEFAULT":0.2},
    {"NAME":"drift","TYPE":"float","MIN":0,"MAX":1,"DEFAULT":0.3},
    {"NAME":"architecture","TYPE":"float","MIN":0,"MAX":1,"DEFAULT":0.1},
    {"NAME":"inversion","TYPE":"float","MIN":0,"MAX":1,"DEFAULT":0},
    {"NAME":"view","TYPE":"float","MIN":0,"MAX":1,"DEFAULT":0.45},
    {"NAME":"glitch","TYPE":"float","MIN":0,"MAX":1,"DEFAULT":0},
    {"NAME":"pressure","TYPE":"float","MIN":0,"MAX":1,"DEFAULT":0},
    {"NAME":"scatter","TYPE":"float","MIN":0,"MAX":1,"DEFAULT":0}
  ],
  "PASSES":[
    {"TARGET":"memory","PERSISTENT":true},
    {}
  ]
}*/

float hash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float chamberNoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    return mix(
        mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),
            mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),
            mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}

float density(vec3 p) {
    float t = TIME * 0.21;
    vec3 q = p * 2.15;
    q.y -= t;
    q.x += sin(q.y*1.3 + q.z + t) * (0.32 + drift*0.75);
    q.z += sin(q.y*0.8 - t) * 0.47;
    float eddy = chamberNoise(q);
    float folds = 0.53*eddy + 0.28*chamberNoise(q*2.13 + 1.7*eddy)
                + 0.13*chamberNoise(q*4.31) + 0.06*chamberNoise(q*8.57);
    float center = 0.38*sin(p.y*1.9+t) + 0.18*p.y;
    vec3 plume = vec3((p.x-center)*1.25, p.y*0.41, p.z*0.72);
    return smoothstep(0.37,0.72,folds) * exp(-dot(plume,plume)*2.9)
           * smoke * 3.4;
}

vec2 boxRange(vec3 ro, vec3 rd, vec3 center, vec3 halfSize) {
    vec3 inv = 1.0 / (sign(rd + vec3(0.000001))*max(abs(rd),vec3(0.000001)));
    vec3 a = (center-halfSize-ro)*inv, b = (center+halfSize-ro)*inv;
    vec3 near = min(a,b), far = max(a,b);
    return vec2(max(near.x,max(near.y,near.z)),min(far.x,min(far.y,far.z)));
}

float chamber(vec2 uv) {
    vec2 p = (uv - 0.5) * vec2(RENDERSIZE.x/RENDERSIZE.y,1.0);
    vec3 ro = vec3((view-0.5)*2.05 + 0.12*sin(TIME*0.34),0.48,4.2-pressure*0.27);
    vec3 forward = normalize(-ro);
    vec3 right = normalize(cross(forward,vec3(0,1,0)));
    vec3 rd = normalize(forward + right*p.x + cross(right,forward)*p.y);
    // Four solid walls share the same lip. Their rear edges move through it.
    float rear = mix(0.88,-1.13,inversion);
    float halfDepth = max(0.025,abs(rear)*0.5);
    float hit = 100.0;
    float surface = 0.0;
    for (int wall=0; wall<4; ++wall) {
        float side = mod(float(wall),2.0)*2.0-1.0;
        bool horizontal = wall < 2;
        vec3 c = horizontal ? vec3(0,side*0.64,rear*0.5)
                            : vec3(side*0.88,0,rear*0.5);
        vec3 h = horizontal ? vec3(0.92,0.043,halfDepth)
                            : vec3(0.043,0.60,halfDepth);
        vec2 range = boxRange(ro,rd,c,h);
        if (range.x > 0.0 && range.x < range.y && range.x < hit) {
            hit = range.x;
            vec3 q = abs((ro+rd*hit-c)/h);
            float face = q.z > 0.995 ? 0.36 : (q.y > 0.995 ? 0.21 : 0.11);
            float recess = clamp(-(ro.z+rd.z*hit),0.0,1.2);
            surface = (face + 0.15*smoothstep(0.87,1.0,max(q.x,q.y)))
                      * exp(-recess*1.45);
        }
    }
    // A face closes the volume on the relief side, then dissolves at the hinge.
    vec2 cap = boxRange(ro,rd,vec3(0,0,rear),vec3(0.84,0.60,0.018));
    float solid = 1.0-smoothstep(0.12,0.48,inversion);
    if (cap.x > 0.0 && cap.x < cap.y && cap.x < hit) {
        float edge = max(abs(ro.x+rd.x*cap.x)/0.84,abs(ro.y+rd.y*cap.x)/0.60);
        float bevel = smoothstep(0.94,1.0,edge);
        surface = mix(surface,0.15+0.16*bevel,solid);
        hit = mix(min(hit,5.85),cap.x,solid);
    }
    float end = mix(5.85,min(hit,5.85),architecture);
    float start = 2.45;
    float stepSize = max(0.0,end-start)/32.0;
    float transmission = 1.0, light = 0.0;
    float jitter = hash(vec3(floor(uv*RENDERSIZE),0.0));
    for (int i=0; i<32; ++i) {
        vec3 pos = ro + rd*(start+(float(i)+jitter)*stepSize);
        float d = density(pos);
        float shadow = density(pos+vec3(-0.15,0.09,0.11));
        float rim = clamp(0.42+(d-shadow)*1.1,0.1,1.65);
        float alpha = 1.0-exp(-d*stepSize);
        light += transmission * alpha * rim;
        transmission *= 1.0-alpha;
    }
    return clamp(light + architecture*surface*transmission,0.0,1.0);
}

void main() {
    vec2 uv = isf_FragNormCoord;
    if (PASSINDEX == 0) {
        vec4 previous = IMG_NORM_PIXEL(memory,uv);
        gl_FragColor = vec4(chamber(uv),previous.r,previous.g,1.0);
    } else {
        float amount = clamp(glitch + scatter*glitch*0.7,0.0,1.0);
        float band = hash(vec3(floor(uv.y*64.0),floor(TIME*12.0),3));
        float shift = (band-0.5) * amount * (band>0.79 ? 0.18 : 0.018);
        vec2 dx = vec2(shift,0);
        vec3 now = IMG_NORM_PIXEL(memory,clamp(uv+dx,0.0,1.0)).rgb;
        float red = IMG_NORM_PIXEL(memory,clamp(uv+dx*1.7,0.0,1.0)).g;
        float blue = IMG_NORM_PIXEL(memory,clamp(uv-dx*0.8,0.0,1.0)).b;
        gl_FragColor = vec4(mix(vec3(now.r),vec3(red,now.r,blue),amount),1.0);
    }
}
