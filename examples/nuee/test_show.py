import json
import re
import tempfile
import unittest
from pathlib import Path
from build_show import build, MOMENTS

ROOT=Path(__file__).resolve().parents[2]

def shader_inputs():
    text=(ROOT/'isf/generators/Particles.fs').read_text(encoding='utf-8')
    header=json.loads(re.search(r'/\*(\{.*?\})\*/',text,re.S).group(1))
    return {i['NAME']:i for i in header['INPUTS']}

class ScoreTest(unittest.TestCase):
    def test_ownership_and_routes(self):
        with tempfile.TemporaryDirectory() as d:
            show,cues=build(d)
            self.assertEqual([c['at'] for c in cues],[m[0] for m in MOMENTS])
            self.assertAlmostEqual(sum(c['duration'] for c in cues),180)
            animated={t['tracks'][0]['param'] for t in show['timelines']}
            driven={i['param'] for i in show['interactions']}
            self.assertFalse(animated & driven,'audio and timeline ownership must not fight')
            for s in show['snapshots']:
                self.assertFalse((animated|driven) & s['layers'][0]['params'].keys(),'cues preserve live motion and audio')
            self.assertEqual(show['snapshots'][-1]['composition']['volume'],0)
            for name in ('Particles.fs','Particles.vs'):self.assertTrue((Path(d)/'shaders'/name).is_file())
    def test_parameters_exist_and_fit(self):
        inputs=shader_inputs()
        with tempfile.TemporaryDirectory() as d:
            show,_=build(d)
            params=dict(show['layers'][0]['params'])
            for s in show['snapshots']:params.update(s['layers'][0]['params'])
            for t in show['timelines']:params[t['tracks'][0]['param']]=t['tracks'][0]['oscillator']['center']
            for i in show['interactions']:params[i['param']]=0
            for key,value in params.items():
                if not key.startswith('param/'):continue
                name=key.split('/')[1]
                self.assertIn(name,inputs,key)
                spec=inputs[name]
                if spec['TYPE']=='float':self.assertTrue(spec['MIN']<=value<=spec['MAX'],key)
                if spec['TYPE']=='long':self.assertIn(value,spec['VALUES'],key)
    def test_camera_travels(self):
        paths={m[3]['camPath'] for m in MOMENTS}
        self.assertEqual(paths,{0,1,2,3,4,5},'every camera path is heard at least once')

if __name__=='__main__':unittest.main()
