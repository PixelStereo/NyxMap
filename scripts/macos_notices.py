#!/usr/bin/env python3
"""The notices of the libraries the macOS package copies from Homebrew.

Usage: macos_notices.py APP

Qt, FFmpeg and the libraries they use (images, fonts, compression…) are copied from Homebrew into
APP/Contents/Frameworks. For each one this finds the Homebrew formula it comes from, and writes
APP/Contents/Resources/THIRD-PARTY-MACOS.md (library, formula, version, licence, home page) and the licence files the
formula installed into APP/Contents/Resources/licenses/macos/<formula>/.

Fails when a library's formula cannot be found, or when its licence is GPL only: the package may not contain one.
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

CELLAR = Path(subprocess.check_output(["brew", "--cellar"], text=True).strip())
# Not from Homebrew: built or fetched for the package, their licences travel with them (OMT-LICENSES.txt, NDI-NOTICE.txt)
OWN = re.compile(r"^lib(omt|vmx|ndi)\b")
FFMPEG = re.compile(r"^lib(avcodec|avdevice|avfilter|avformat|avutil|swresample|swscale)\.\d+\.dylib$")
# Libraries whose licence is not their formula's: gettext's programs are GPL, its libintl is LGPL (gettext's
# COPYING and its manual, "The GNU libintl library is covered by the GNU Lesser General Public License")
LIBRARY_LICENCE = {"libintl": "LGPL-2.1-or-later"}
# The licence texts the package already has, for a formula that installs none (Qt's modules)
SHIPPED_TEXTS = {"LGPL-3.0": "licenses/LGPL-3.0.txt", "LGPL-2.1": "licenses/LGPL-2.1.txt", "GPL-3.0": "licenses/GPL-3.0.txt"}
LICENCE_FILE = re.compile(r"^(LICEN[CS]E|COPYING|COPYRIGHT|NOTICE|UNLICENSE|LGPL|GPL)", re.I)


def is_macho(p: Path):
    return p.is_file() and not p.is_symlink() and "Mach-O" in subprocess.run(
        ["file", "-b", str(p)], text=True, stdout=subprocess.PIPE).stdout


def libraries(frameworks: Path):
    """name -> path: the dylibs, and each framework once (by its bundle name)"""
    found = {}
    for p in sorted(frameworks.rglob("*")):
        fw = next((a for a in p.parents if a.suffix == ".framework"), None)
        if fw is not None:
            found.setdefault(fw.name, fw)
        elif p.suffix == ".dylib" and is_macho(p):
            found[p.name] = p
    return found


def formula_of(name: str):
    """The Homebrew formulas whose keg has this library or framework (a real one before a link to another keg's)"""
    hits = {}
    for pattern in (f"*/*/lib/{name}", f"*/*/lib/**/{name}", f"*/*/Frameworks/**/{name}"):
        for p in CELLAR.glob(pattern):
            formula = p.relative_to(CELLAR).parts[0]
            hits[formula] = hits.get(formula, False) or not p.is_symlink() or p.resolve().is_relative_to(CELLAR / formula)
    real = sorted(f for f, own in hits.items() if own)
    return real or sorted(hits)


def flatten(licence):
    if licence is None:
        return []
    if isinstance(licence, str):
        return [licence]
    if isinstance(licence, list):
        return [t for x in licence for t in flatten(x)]
    if isinstance(licence, dict):
        return [t for k, v in licence.items() for t in ([k] if k not in ("any_of", "all_of", "with") else []) + flatten(v)]
    return [str(licence)]


def describe(licence):
    if licence is None:
        return "not stated"
    if isinstance(licence, str):
        return licence
    if isinstance(licence, dict) and len(licence) == 1:
        (k, v), = licence.items()
        if k in ("any_of", "all_of"):
            return f" {'or' if k == 'any_of' else 'and'} ".join(f"({describe(x)})" if isinstance(x, dict) else describe(x)
                                                                  for x in v)
        return f"{k} with {describe(v.get('with')) if isinstance(v, dict) else v}"
    return json.dumps(licence)


def gpl_only(licence):
    """GPL (not LGPL) with no other choice"""
    if isinstance(licence, dict) and "any_of" in licence:
        return all(gpl_only(x) for x in licence["any_of"])
    return any(re.match(r"^A?GPL", t) for t in flatten(licence))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    app = Path(sys.argv[1]).resolve()
    resources = app / "Contents" / "Resources"
    out = resources / "licenses" / "macos"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    by_formula, errors = {}, []
    for name in libraries(app / "Contents" / "Frameworks"):
        if OWN.match(name):
            continue
        if FFMPEG.match(name):  # rebuilt without GPL parts (build_ffmpeg_lgpl.py), not Homebrew's
            by_formula.setdefault("ffmpeg", []).append(name)
            continue
        formulas = formula_of(name)
        if len(formulas) != 1:
            errors.append(f"{name}: Homebrew formula {'not found' if not formulas else 'ambiguous: ' + ', '.join(formulas)}")
            continue
        by_formula.setdefault(formulas[0], []).append(name)
    if errors:
        sys.exit("\n".join(errors))

    info = {f["name"]: f for f in json.loads(subprocess.check_output(
        ["brew", "info", "--json=v2", *sorted(by_formula)], text=True))["formulae"]}
    rows = []
    for formula in sorted(by_formula):
        f = info[formula]
        version = (f.get("installed") or [{}])[0].get("version") or f["versions"]["stable"]
        licence = f.get("license")
        stems = {re.sub(r"\.\d+.*$", "", n) for n in by_formula[formula]}
        if formula == "ffmpeg":
            licence = "LGPL-2.1-or-later"  # this build: no --enable-gpl
        elif stems <= LIBRARY_LICENCE.keys() and len({LIBRARY_LICENCE[x] for x in stems}) == 1:
            licence = LIBRARY_LICENCE[next(iter(stems))]
        elif gpl_only(licence):
            errors.append(f"{formula} ({', '.join(by_formula[formula])}) is {describe(licence)}: GPL only")
        keg = CELLAR / formula / version
        copied = []
        for p in sorted(keg.iterdir()) if keg.is_dir() else []:
            if p.is_file() and LICENCE_FILE.match(p.name):
                (out / formula).mkdir(exist_ok=True)
                shutil.copy2(p, out / formula / p.name)
                copied.append(p.name)
        texts = [f"licenses/macos/{formula}/{n}" for n in copied]
        if not copied:
            texts = sorted({t for spdx, t in SHIPPED_TEXTS.items()
                            if any(tok.startswith(spdx) for tok in flatten(licence))})
            if not texts:
                print(f"warning: {formula}: no licence file in its keg ({keg})")
        texts = ", ".join(f"`{t}`" for t in texts) or "see its home page"
        rows.append(f"| {formula} | {version} | {describe(licence)} | {texts} | {f.get('homepage', '')} | "
                    f"{', '.join(sorted(by_formula[formula]))} |")
    if errors:
        sys.exit("\n".join(errors))

    (resources / "THIRD-PARTY-MACOS.md").write_text(
        "# Libraries in the macOS package\n\n"
        "Besides the components listed in THIRD-PARTY-NOTICES.md, the macOS package contains these libraries, copied "
        "from Homebrew (https://brew.sh) when it was built. FFmpeg is built from source without its GPL components "
        "(LGPL v2.1 or later); its version and configuration are in DEPENDENCIES.txt.\n\n"
        "| Formula | Version | Licence | Licence text | Home page | Libraries |\n|---|---|---|---|---|---|\n"
        + "\n".join(rows) + "\n", encoding="utf-8")
    print("\n".join(rows))
    print(f"{len(rows)} formulas, {sum(len(v) for v in by_formula.values())} libraries")


if __name__ == "__main__":
    main()
