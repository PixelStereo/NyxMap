"""Port Ghost Arcade's single-pass GLSL effects to ISF 2.0 filters (isf/effects/Ghost Arcade).

Usage: python3 scripts/port_ghost_arcade.py <ghost-arcade checkout> "isf/effects/Ghost Arcade"

Reads the Ghost Arcade sources (AGPL-3.0) and writes one .fs per effect whose shader only samples the layer
(uTexture). Uniforms become ISF inputs: ranges and labels from EFFECT_PARAM_DEFS when the effect exposes the
parameter, otherwise the material's default as a constant. Prints what was skipped and why.
"""
import json
import re
import sys
from pathlib import Path

repo = Path(sys.argv[1])
out = Path(sys.argv[2])
renderer = repo / "src/lib/renderer"

# Shader constants: name -> GLSL source (template literals without interpolation only)
shaders = {}
for path in [renderer / "shaders.ts", *sorted((renderer / "shaders").glob("*.ts"))]:
    text = path.read_text()
    for m in re.finditer(r"export const (\w+)\s*=\s*/\*\s*glsl\s*\*/\s*`([\s\S]*?)`;", text):
        if "${" not in m.group(2):
            shaders[m.group(1)] = m.group(2)

effects_ts = (renderer / "effects.ts").read_text()

# effect type -> shader constant
block = effects_ts[effects_ts.index("export const effectShaders"):]
block = block[:block.index("\n};")]
shader_of = dict(re.findall(r"^\s+(\w+):\s*(\w+),", block, re.M))


def switch_cases(text, start_marker):
    """case 'a': case 'b': ... body -> {type: body} for one switch."""
    start = text.index("switch (", text.index(start_marker))
    body = text[start:]
    end = re.search(r"\n  }\n", body)
    body = body[:end.start()] if end else body
    cases = {}
    pending = []
    for part in re.split(r"(\n\s+case '(\w+)':)", body):
        pass
    for m in re.finditer(r"case '(\w+)':([\s\S]*?)(?=\n\s+case '\w+':|\Z)", body):
        pending.append(m.group(1))
        if re.sub(r"//[^\n]*", "", m.group(2)).strip():
            for t in pending:
                cases[t] = m.group(2)
            pending = []
    return cases


material = switch_cases(effects_ts, "export function createEffectMaterial")
updates = switch_cases(effects_ts, "export function updateEffectUniforms")


mode_maps = {}
for path in [renderer / "shaders.ts", *sorted((renderer / "shaders").glob("*.ts"))]:
    for m in re.finditer(r"export const (\w+): Record<string, number> = \{([\s\S]*?)\};", path.read_text()):
        mode_maps[m.group(1)] = {k: float(v) for k, v in re.findall(r"(\w+):\s*([-\d.]+)", m.group(2))}


def number(expr):
    expr = expr.strip()
    lookup = re.match(r"(\w+)\['(\w+)'\]\s*\?\?\s*([-\d.]+)$", expr)
    if lookup:
        return mode_maps.get(lookup.group(1), {}).get(lookup.group(2), float(lookup.group(3)))
    try:
        return float(eval(expr, {"__builtins__": {}}, {"Math": None}))
    except Exception:
        return None


def defaults_for(t):
    result = {}
    for m in re.finditer(r"uniforms\.(\w+)\s*=\s*\{\s*value:\s*([^}]*?)\s*\}", material.get(t, "")):
        value = m.group(2)
        vec = re.match(r"new THREE\.Vector([234])\(([^)]*)\)", value)
        if vec:
            result[m.group(1)] = [number(v) for v in vec.group(2).split(",")] if vec.group(2).strip() else None
        elif value in ("true", "false"):
            result[m.group(1)] = value == "true"
        else:
            result[m.group(1)] = number(value)
    return result


def params_for(t):
    """uniform -> the first parameter of its assignment chain that the effect exposes (p.a ?? p.b ?? 1)."""
    defs = param_defs(t)
    result = {}
    for m in re.finditer(r"u\.(\w+)\.value\s*=\s*((?:p\.\w+\s*(?:\?\?\s*)?)+)[^;\n]*;", updates.get(t, "")):
        for param in re.findall(r"p\.(\w+)", m.group(2)):
            if param in defs:
                result[m.group(1)] = param
                break
    return result


defs_ts = (repo / "src/lib/effects/effectParamDefs.ts").read_text()


def param_defs(t):
    m = re.search(r"\n  " + re.escape(t) + r": \[([\s\S]*?)\n  \],", defs_ts)
    result = {}
    if not m:
        return result
    for e in re.finditer(r"\{ name: '([^']*)', param: '(\w+)', min: ([-\d.e]+), max: ([-\d.e]+), step: [-\d.e]+, "
                         r"default: ([-\d.e]+)([^\n]*)", m.group(1)):
        options = re.findall(r"\{ value: ([-\d.]+), label: '([^']*)' \}", e.group(6))
        # options may continue on the next lines
        if "type: 'select'" in e.group(6) and not options:
            tail = m.group(1)[e.end():]
            tail = tail[:tail.index("] }") + 3] if "] }" in tail else ""
            options = re.findall(r"\{ value: ([-\d.]+), label: '([^']*)' \}", tail)
        result[e.group(2)] = {"label": e.group(1), "min": float(e.group(3)), "max": float(e.group(4)),
                              "default": float(e.group(5)), "options": options}
    return result


catalog_ts = (repo / "src/lib/effects/effectCatalog.ts").read_text()
catalog = {m.group(1): (m.group(2), m.group(3), m.group(4)) for m in re.finditer(
    r"type: '(\w+)', label: '([^']*)', category: '([^']*)',\s*description: '((?:[^'\\]|\\.)*)'", catalog_ts)}

BASE = {"uTexture", "uResolution", "uTime"}
SOURCE_HINT = re.compile(r"shadertoy|adapted from|ported from|inspired by|\bcredits?\b|\bauthor|copyright|licen[sc]e|"
                         r"https?://|iquilezles", re.I)
AUDIO = re.compile(r"audio|beat|bass|treble|\bmid\b|fft|spectrum|level", re.I)


GLSL_NAMES = set("""mix step smoothstep clamp min max abs sign floor ceil fract mod pow exp exp2 log log2 sqrt
inversesqrt sin cos tan asin acos atan radians degrees length distance dot cross normalize reflect refract
faceforward texture texture2D lessThan greaterThan equal notEqual any all not matrixCompMult position
noise1 noise2 noise3 noise4 dFdx dFdy fwidth round trunc sinh cosh tanh transpose determinant inverse
TIME TIMEDELTA RENDERSIZE PASSINDEX FRAMEINDEX DATE main input output sample filter active""".split())


def label_of(uniform):
    words = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", uniform[1:] if uniform.startswith("u") else uniform)
    return words[:1].upper() + words[1:]


def input_name(uniform):
    name = uniform[1:] if uniform.startswith("u") and uniform[1:2].isupper() else uniform
    return name[:1].lower() + name[1:]
written, skipped = [], []
out.mkdir(parents=True, exist_ok=True)
for t, (label, category, description) in catalog.items():
    const = shader_of.get(t)
    glsl = shaders.get(const or "")
    if not glsl:
        skipped.append((t, "no standalone GLSL shader (WebGPU, multi-pass or generated in code)"))
        continue
    uniforms = []
    declarations = [d for line in glsl.split("\n")
                    for d in re.finditer(r"\buniform\s+(\w+)\s+([^;]+);", line.split("//")[0])]
    for decl in declarations:
        for item in decl.group(2).split(","):
            item = item.strip()
            m = re.match(r"(\w+)\s*(\[[^\]]*\])?$", item)
            if m:
                uniforms.append((decl.group(1), m.group(1), m.group(2)))
    samplers = [n for ty, n, _ in uniforms if ty.startswith("sampler") and n != "uTexture"]
    if samplers:
        skipped.append((t, "needs other textures: " + ", ".join(samplers)))
        continue
    if any(arr for _, _, arr in uniforms):
        skipped.append((t, "array uniforms"))
        continue
    defaults, mapping, defs = defaults_for(t), params_for(t), param_defs(t)
    inputs = [{"NAME": "inputImage", "TYPE": "image"}]
    defines = []
    used_names = {"inputImage"}
    problem = None
    for ty, name, _ in uniforms:
        if name in BASE:
            continue
        param = mapping.get(name)
        d = defs.get(param) if param else None
        if d and ty in ("float", "int", "bool"):
            isf_name = param
            if isf_name in used_names:
                isf_name = name[1:]
            used_names.add(isf_name)
            if d["options"]:
                values = [int(float(v)) for v, _ in d["options"]]
                inputs.append({"NAME": isf_name, "LABEL": d["label"], "TYPE": "long", "VALUES": values,
                               "LABELS": [l for _, l in d["options"]], "DEFAULT": int(d["default"])})
                cast = {"float": "float", "int": "int", "bool": "bool"}[ty]
                defines.append(f"#define {name} {cast}({isf_name})")
            elif ty == "bool" or (d["min"] == 0 and d["max"] == 1 and d.get("step") == 1):
                inputs.append({"NAME": isf_name, "LABEL": d["label"], "TYPE": "bool", "DEFAULT": bool(d["default"])})
                defines.append(f"#define {name} {'' if ty == 'bool' else ty}({isf_name})" if ty != "bool"
                               else f"#define {name} {isf_name}")
            else:
                inputs.append({"NAME": isf_name, "LABEL": d["label"], "TYPE": "float", "MIN": d["min"],
                               "MAX": d["max"], "DEFAULT": d["default"]})
                defines.append(f"#define {name} {'int' if ty == 'int' else ''}({isf_name})")
            continue
        value = defaults.get(name)
        # Exposed with a range around its default, unless the engine drives it (audio, beat)
        if ty in ("float", "int") and isinstance(value, (int, float)) and not AUDIO.search(name):
            isf_name = input_name(name)
            # Never a GLSL built-in (mix, step…), an engine name (position) or a word the shader already uses
            if (isf_name in used_names or isf_name in GLSL_NAMES or isf_name.startswith(("isf_", "gl_"))
                    or re.search(r"\b" + re.escape(isf_name) + r"\b", glsl)):
                isf_name += "Value"
            used_names.add(isf_name)
            v = float(value)
            low, high = (min(0.0, v * 2), max(1.0, v * 2)) if v >= 0 else (v * 2, -v * 2)
            if name.endswith(("Mode", "Shape", "Style", "Palette", "Type")) or ty == "int":
                inputs.append({"NAME": isf_name, "LABEL": label_of(name), "TYPE": "float", "MIN": 0.0,
                               "MAX": max(8.0, v), "DEFAULT": v})
                defines.append(f"#define {name} {'int' if ty == 'int' else 'float'}(floor({isf_name} + 0.5))")
            else:
                inputs.append({"NAME": isf_name, "LABEL": label_of(name), "TYPE": "float", "MIN": low,
                               "MAX": high, "DEFAULT": v})
                defines.append(f"#define {name} ({isf_name})")
            continue
        if ty == "float":
            defines.append(f"#define {name} {float(value if isinstance(value, (int, float)) and value is not None else 0.0)!r}")
        elif ty == "int":
            defines.append(f"#define {name} {int(value or 0)}")
        elif ty == "bool":
            defines.append(f"#define {name} {'true' if value else 'false'}")
        elif ty in ("vec2", "vec3", "vec4") and isinstance(value, list) and all(v is not None for v in value):
            defines.append(f"#define {name} {ty}({', '.join(repr(float(v)) for v in value)})")
        else:
            problem = f"uniform {ty} {name} has no usable value"
            break
    if problem:
        skipped.append((t, problem))
        continue
    body = glsl
    while True:  # every declaration, several may share a line; never inside a comment
        stripped = re.sub(r"^([^/\n]*?)\buniform\s+\w+\s+[^;\n]+;", r"\1", body, flags=re.M)
        if stripped == body:
            break
        body = stripped
    # Words reserved by desktop GLSL (1.50 and later) or its built-in functions, used as names in WebGL code
    for word in ("active", "noise1", "noise2", "noise3", "noise4", "filter", "sample", "partition", "common",
                 "input", "output", "superp", "resource", "patch", "subroutine"):
        body = re.sub(r"\b" + word + r"\b(?!\s*:)", word + "_", body)
    body = re.sub(r"^\s*varying\s+vec2\s+vUv\s*;[^\n]*\n", "", body, flags=re.M)
    body = re.sub(r"^\s*precision\s+\w+\s+float\s*;[^\n]*\n", "", body, flags=re.M)
    body = re.sub(r"\btexture2D\s*\(\s*uTexture\s*,", "IMG_NORM_PIXEL(inputImage,", body)
    body = re.sub(r"\btexture\s*\(\s*uTexture\s*,", "IMG_NORM_PIXEL(inputImage,", body)
    if re.search(r"\buTexture\b", body):
        skipped.append((t, "passes the texture to a function"))
        continue
    lines = body.split("\n")
    indent = min((len(l) - len(l.lstrip()) for l in lines if l.strip()), default=0)
    body = "\n".join(l[indent:] for l in lines).strip("\n")
    hints = sorted({l.strip() for l in glsl.split("\n") if SOURCE_HINT.search(l) and l.strip().startswith("//")})
    adapted_from_third_party = bool(hints)
    # Adapted from Ghost Arcade's code: both names. Origins the original cites are kept in the source notes.
    credit = "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo"
    if adapted_from_third_party:
        credit += " — see the source notes for earlier origins"
    header = {
        "ISFVSN": "2",
        "DESCRIPTION": description.replace("\\'", "'").rstrip(".") + ". Adapted from Ghost Arcade's " + label + " effect.",
        "CREDIT": credit,
        "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
        "CATEGORIES": [category, "Ghost Arcade"],
        "INPUTS": inputs,
    }
    prelude = ["#define vUv isf_FragNormCoord", "#define uTime TIME", "#define uResolution RENDERSIZE", *defines]
    text = "/*" + json.dumps(header, indent=4, ensure_ascii=False) + "*/\n\n"
    text += "// Ported from Ghost Arcade (" + const + ") by Pixel Stereo. Licensed under the AGPL-3.0.\n"
    if hints:
        text += "// Source notes kept from the original:\n" + "\n".join("//   " + h.lstrip("/ ") for h in hints) + "\n"
    text += "\n".join(prelude) + "\n\n" + body + "\n"
    safe = re.sub(r"[\\/:*?\"<>|]", "-", label)
    (out / f"{safe}.fs").write_text(text)
    written.append((t, safe, category, bool(hints)))

print(f"written {len(written)}, skipped {len(skipped)}")
json.dump({"written": written, "skipped": skipped}, open(out.parent / "port-report.json", "w"), indent=1)
