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

float smokeNoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f*f*f*(f*(f*6.0-15.0)+10.0);
    return mix(
        mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),
            mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),
            mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}

// Rotate successive octaves to keep the lattice out of the visible wisps.
const mat3 octaveRotation = mat3(0.00, 0.80, 0.60,
                                -0.80, 0.36, -0.48,
                                -0.60, -0.48, 0.64);

float smokeDensity(vec3 p, bool detail) {
    float t = TIME * flow;
    float height = clamp((p.y+2.0)*0.25,0.0,1.0);
    vec3 q = p;
    q.x -= 0.28*sin(1.6*p.y-0.8*t) + 0.16*p.y;
    q.z -= 0.22*cos(2.0*p.y-t);
    float width = spread * mix(0.24,0.93,height);
    float radius = length(q.xz / vec2(width,width*0.80));
    if (radius > 2.4 || p.y < -2.1 || p.y > 2.2) return 0.0;

    // Advect coherent pockets upwards, then erode their edges at finer scales.
    vec3 a = q * vec3(2.8,1.7,2.8) - vec3(0,1.7*t,0);
    vec3 warp = vec3(smokeNoise(a+vec3(13,0,0)), smokeNoise(a+vec3(0,19,0)),
                     smokeNoise(a+vec3(0,0,29))) - 0.5;
    a += (0.65+1.9*turbulence)*warp;
    float coarse = smokeNoise(a);
    vec3 b = octaveRotation*a*2.04;
    float structure = 0.70*coarse + 0.30*smokeNoise(b);
    if (detail) {
        b = octaveRotation*b*2.11;
        structure += 0.17*(smokeNoise(b)-0.5);
        structure += 0.08*(smokeNoise(octaveRotation*b*2.03)-0.5);
    }
    float taper = smoothstep(-2.1,-1.45,p.y) * (1.0-smoothstep(0.8,2.2,p.y));
    return smoothstep(0.34+0.22*radius,0.68+0.14*radius,structure)
           * exp(-0.72*radius*radius) * taper * densityAmount * 6.5;
}

void main() {
    vec2 uv = (isf_FragNormCoord-0.5)*vec2(RENDERSIZE.x/RENDERSIZE.y,1.0);
    vec3 origin = vec3(0,0,4.4);
    vec3 direction = normalize(vec3(uv,-1));
    vec3 lightDirection = normalize(vec3(-0.65,0.44,-0.62));
    const float stepSize = 0.068;
    float transmission = 1.0;
    float light = 0.0;
    for (int i=0; i<56; ++i) {
        vec3 p = origin + direction*(2.65+(float(i)+0.5)*stepSize);
        float d = smokeDensity(p,true);
        if (d < 0.004) continue;
        // Trace optical depth toward a broad backlight through the volume.
        float shadow = 0.19*smokeDensity(p+lightDirection*0.14,false)
                     + 0.29*smokeDensity(p+lightDirection*0.43,false)
                     + 0.47*smokeDensity(p+lightDirection*0.81,false);
        float illumination = 0.16+0.84*exp(-1.9*shadow);
        float alpha = 1.0-exp(-d*stepSize);
        light += transmission*alpha*illumination;
        transmission *= 1.0-alpha;
        if (transmission < 0.005) break;
    }
    float alpha = 1.0-transmission;
    gl_FragColor = vec4(tint.rgb*clamp(light/max(alpha,0.0001),0.0,1.0),alpha*tint.a);
}
