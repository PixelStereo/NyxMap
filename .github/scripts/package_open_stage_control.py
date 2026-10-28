"""Package the ready-to-load repository files; never ship node_modules or build tools."""
import argparse
from pathlib import Path
import shutil
import zipfile

FILES = ('nyxmap.json', 'nyxmap.js', 'nyxmap-layers.cjs', 'nyxmap-dependencies.cjs',
         'nyxmap-dependencies.LICENSE.txt', 'README.md')


def package(repository, destination):
    source = repository / 'integrations/open-stage-control'
    folder = destination / 'NyxMap-OpenStageControl'
    folder.mkdir(parents=True, exist_ok=False)
    for name in FILES:
        shutil.copyfile(source / name, folder / name)
    # Relative repository links do not exist beside a standalone release ZIP.
    readme = folder / 'README.md'
    text = readme.read_text(encoding='utf-8').replace('](../../docs/',
        '](https://github.com/PixelStereo/NyxMap/blob/main/docs/')
    readme.write_text(text, encoding='utf-8')
    shutil.copyfile(repository / 'LICENSE', folder / 'LICENSE')
    archive = destination / 'NyxMap-OpenStageControl.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
        for path in sorted(folder.iterdir()):
            output.write(path, Path(folder.name) / path.name)
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    args.destination.mkdir(parents=True, exist_ok=True)
    print(package(Path(__file__).resolve().parents[2], args.destination))
