"""Package the ready-to-load repository files; never ship node_modules or build tools."""
import argparse
from pathlib import Path
import shutil
import zipfile

FILES = ('pinol.json', 'pinol.js', 'pinol-layers.cjs', 'pinol-dependencies.cjs',
         'pinol-dependencies.LICENSE.txt', 'README.md')


def package(repository, destination):
    source = repository / 'integrations/open-stage-control'
    folder = destination / 'Pinol-OpenStageControl'
    folder.mkdir(parents=True, exist_ok=False)
    for name in FILES:
        shutil.copyfile(source / name, folder / name)
    shutil.copyfile(repository / 'LICENSE', folder / 'LICENSE')
    archive = destination / 'Pinol-OpenStageControl.zip'
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
