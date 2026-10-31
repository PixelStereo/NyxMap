#!/usr/bin/env python3
"""Download the two pinned OpenCV Zoo MediaPipe models, retaining TLS and SHA verification."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import urllib.request
import urllib.error

def valid(path, model):
    return path.is_file() and path.stat().st_size == model['size'] and hashlib.sha256(path.read_bytes()).hexdigest() == model['sha256']

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', required=True, type=Path, help='Select this folder in Interactions > Hands afterwards')
    parser.add_argument('--replace', action='store_true', help='Replace existing files with unexpected checksums')
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / 'third_party/hand-models/models.json').read_text())
    args.directory.mkdir(parents=True, exist_ok=True)
    for model in manifest['models']:
        target = args.directory / model['name']
        if valid(target, model):
            print(f'{model["name"]}: verified, already present')
            continue
        if target.exists() and not args.replace:
            raise SystemExit(f'{target}: unexpected checksum; use --replace to replace it')
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=args.directory, delete=False) as output:
                temporary = Path(output.name)
                with urllib.request.urlopen(model['url'], timeout=60) as response:
                    while chunk := response.read(1024 * 1024):
                        output.write(chunk)
            if not valid(temporary, model):
                raise ValueError(f'{model["name"]}: checksum or size mismatch')
            temporary.replace(target)
            print(f'{model["name"]}: downloaded and SHA-256 verified')
        finally:
            if temporary and temporary.exists():
                temporary.unlink()
    print('Select this model folder in NyxMap > Interactions > Hands:', args.directory.resolve())
if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, urllib.error.URLError) as error:
        raise SystemExit(f'Hand model download failed: {error}') from None
