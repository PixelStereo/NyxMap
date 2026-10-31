"""Exercise download integrity/atomicity without network access."""
import hashlib
import importlib.util
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch
import io

spec = importlib.util.spec_from_file_location('download_hand_models', Path(__file__).resolve().parents[1] / 'scripts/download_hand_models.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
payload = b'fake verified model bytes'
model = {'name': 'test.onnx', 'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest(), 'url': 'https://example.invalid/model'}

with tempfile.TemporaryDirectory() as directory:
    target = Path(directory) / model['name']
    manifest = '{"models": [' + __import__('json').dumps(model) + ']}'
    with patch.object(sys, 'argv', ['download', '--directory', directory]), patch.object(Path, 'read_text', return_value=manifest):
        with patch.object(module.urllib.request, 'urlopen', return_value=io.BytesIO(payload)):
            module.main()
        assert target.read_bytes() == payload
        with patch.object(module.urllib.request, 'urlopen', side_effect=AssertionError('cache should avoid network')):
            module.main()
        target.write_bytes(b'user file')
        try:
            module.main()
            raise AssertionError('unexpected existing file overwritten')
        except SystemExit as error:
            assert '--replace' in str(error)
        assert target.read_bytes() == b'user file'
    with patch.object(sys, 'argv', ['download', '--directory', directory, '--replace']), patch.object(Path, 'read_text', return_value=manifest), patch.object(module.urllib.request, 'urlopen', return_value=io.BytesIO(b'bad download')):
        try:
            module.main()
            raise AssertionError('invalid payload accepted')
        except ValueError:
            pass
    assert target.read_bytes() == b'user file'
    assert list(Path(directory).iterdir()) == [target], 'temporary partial download leaked'
print('Download verification, cache, existing-file protection and failed-download atomicity passed')
