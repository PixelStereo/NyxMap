/*{
  "ISFVSN":"2",
  "DESCRIPTION":"Self-shaded volumetric smoke with a transparent background.",
  "CATEGORIES":["Atmosphere","Noise"],
  "CREDITS":["Pixel Stereo"],
  "INPUTS":[
    {"NAME":"densityAmount","LABEL":"Density","TYPE":"float","MIN":0,"MAX":2,"DEFAULT":0.65},
    {"NAME":"turbulence","TYPE":"float","MIN":0,"MAX":1,"DEFAULT":0.4},
    {"NAME":"flow","LABEL":"Rise speed","TYPE":"float","MIN":0,"MAX":2,"DEFAULT":0.25},
    {"NAME":"spread","TYPE":"float","MIN":0.35,"MAX":2,"DEFAULT":1},
    {"NAME":"tint","TYPE":"color","DEFAULT":[1,1,1,1]}
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

float smokeDensity(vec3 p) {
    float t = TIME * flow;
    vec3 q = p * 2.15;
    q.y -= t;
    q.x += sin(q.y*1.3 + q.z + t) * (0.32 + turbulence*0.75);
    q.z += sin(q.y*0.8 - t) * 0.47;
    float eddy = chamberNoise(q);
    float folds = 0.53*eddy + 0.28*chamberNoise(q*2.13 + 1.7*eddy)
                + 0.13*chamberNoise(q*4.31) + 0.06*chamberNoise(q*8.57);
    float center = 0.38*sin(p.y*1.9+t) + 0.18*p.y;
    vec3 plume = vec3((p.x-center)*1.25/spread, p.y*0.41, p.z*0.72);
    return smoothstep(0.37,0.72,folds) * exp(-dot(plume,plume)*2.9)
           * densityAmount * 3.4;
}

void main() {
    vec2 uv = (isf_FragNormCoord-0.5)*vec2(RENDERSIZE.x/RENDERSIZE.y,1.0);
    vec3 origin = vec3(0,0,4);
    vec3 direction = normalize(vec3(uv,-1));
    const float stepSize = 0.108;
    float jitter = hash(vec3(floor(gl_FragCoord.xy),0.0));
    float transmission = 1.0;
    float light = 0.0;
    for (int i=0; i<32; ++i) {
        vec3 p = origin + direction*(2.35+(float(i)+jitter)*stepSize);
        float d = smokeDensity(p);
        float shadow = smokeDensity(p+vec3(-0.15,0.09,0.11));
        float rim = clamp(0.42+(d-shadow)*1.1,0.1,1.65);
        float alpha = 1.0-exp(-d*stepSize);
        light += transmission*alpha*rim;
        transmission *= 1.0-alpha;
    }
    float alpha = 1.0-transmission;
    gl_FragColor = vec4(tint.rgb*clamp(light/max(alpha,0.0001),0.0,1.0),alpha*tint.a);
}
