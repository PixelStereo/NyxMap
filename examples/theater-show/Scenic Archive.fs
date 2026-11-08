/*{
  "ISFVSN":"2",
  "DESCRIPTION":"Calibration, lighting and horizon layers for Theater Show.",
  "CATEGORIES":["Generator"],
  "INPUTS":[
    {"NAME":"plate","TYPE":"long","DEFAULT":0,"VALUES":[0,1,2,3],"LABELS":["Calibration","Horizon","Light on shutters","Horizon reflection"]},
    {"NAME":"origin","TYPE":"point2D","DEFAULT":[0,0]},
    {"NAME":"extent","TYPE":"point2D","DEFAULT":[1,1]},
    {"NAME":"exposure","TYPE":"float","DEFAULT":0.7,"MIN":0,"MAX":1.5},
    {"NAME":"failure","TYPE":"float","DEFAULT":0,"MIN":0,"MAX":1},
    {"NAME":"dawn","TYPE":"float","DEFAULT":0,"MIN":0,"MAX":1},
    {"NAME":"sweep","TYPE":"float","DEFAULT":0,"MIN":0,"MAX":1},
    {"NAME":"mire","TYPE":"float","DEFAULT":0,"MIN":0,"MAX":1}
  ]
}*/

float hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * .1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float noise(vec2 p) {
    vec2 f = fract(p), i = floor(p);
    f = f*f*(3.0-2.0*f);
    return mix(mix(hash(i), hash(i+vec2(1,0)), f.x), mix(hash(i+vec2(0,1)),hash(i+1.0),f.x),f.y);
}

float terrain(vec2 p) {
    return noise(p)*.56 + noise(2.08*p)*.25 + noise(4.22*p)*.13 + noise(8.38*p)*.06;
}

vec3 landscape(vec2 uv) {
    vec2 p = vec2((uv.x-.5)*1.78, uv.y);
    float horizon = .50;
    vec2 light = vec2(.29,.64);
    float distanceToSun = length(p-light);
    vec3 night = mix(vec3(.013,.023,.034), vec3(.056,.096,.125), 1.0-smoothstep(horizon,1.0,p.y));
    vec3 morning = mix(vec3(.25,.38,.48), vec3(.98,.62,.31), exp(-7.0*abs(p.y-horizon)));
    vec3 col = mix(night, morning, dawn);
    float cloud = terrain(vec2(p.x*2.0 + TIME*.014, p.y*15.0));
    col *= 1.0 - .40 * smoothstep(.50,.80,cloud);
    col += dawn * vec3(1,.73,.39) * (.22*exp(-7.0*distanceToSun) + .65*(1.0-smoothstep(.025,.027,distanceToSun)));
    for (int i=0; i<4; ++i) {
        float depth = float(i);
        float ridge = horizon + .115*terrain(vec2(p.x*(3.0+depth*1.6)+depth*8.9,depth*2.13)) - depth*.025;
        vec3 rock = mix(vec3(.19,.22,.25),vec3(.013,.026,.033),depth/3.0);
        rock *= .86 + .24*terrain(vec2(p.x*16.0,p.y*26.0)+depth);
        rock += .11*exp(-65.0*abs(p.y-ridge))*vec3(.88,.58,.34)*dawn;
        col = mix(col, rock*(.39+.61*dawn), 1.0-smoothstep(ridge-.002,ridge+.002,p.y));
    }
    float basin = 1.0-smoothstep(.30,.395,p.y);
    float water = terrain(vec2(p.x*5.0 + TIME*.03,p.y*140.0));
    vec3 reflection = mix(vec3(.018,.034,.045),vec3(.32,.19,.095),
                          dawn * exp(-10.0*abs(p.x-light.x)) * (.25+.75*water));
    col = mix(col, reflection*(.62+.38*water), basin);
    return col;
}

void main() {
    vec2 local = isf_FragNormCoord;
    vec2 uv = origin + local*extent;
    float edge = min(min(local.x,1.0-local.x),min(local.y,1.0-local.y));
    float blade = exp(-abs(uv.x + .18*uv.y - mix(-.18,1.30,sweep))*52.0);
    if (plate==2) {
        float glow = blade * smoothstep(.0,.06,sweep) * (1.0-smoothstep(.91,1.0,sweep));
        float seam = .5 + .025*sin(uv.y*22.0) + .006*sin(uv.y*84.0);
        float crack = exp(-abs(uv.x-seam)*240.0)*smoothstep(.23,.56,uv.y);
        gl_FragColor = vec4(vec3(.88,.44,.20), clamp(glow*.42+failure*crack*.38,0.0,1.0));
        return;
    }
    vec3 color;
    if (plate==3) {
        float ripple = terrain(vec2(local.x*12.0,local.y*42.0+TIME*.25))-.5;
        vec2 reflected = vec2(mix(.27,.73,local.x)+ripple*.022, .49+.19*(1.0-local.y)+ripple*.015);
        color = landscape(reflected)*vec3(.40,.33,.28)*pow(local.y,1.5);
    } else {
        color = plate==1 ? landscape(uv) : vec3(0.0);
    }
    float grain = hash(gl_FragCoord.xy + floor(TIME*12.0)) - .5;
    color = max(vec3(0), color*exposure + grain*.009);
    color *= .83+.17*smoothstep(0.0,.026,edge);
    vec2 cell = abs(fract(local*vec2(8,6)+.5)-.5);
    float grid = exp(-min(cell.x,cell.y)*95.0);
    float border = exp(-abs(edge-.011)*340.0);
    vec3 calibration = vec3(.24,.65,.79)*(.055+.25*grid+.65*border);
    color = mix(color,calibration,mire);
    gl_FragColor = vec4(1.0-exp(-2.1*color),1);
}
