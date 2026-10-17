import ast
from pathlib import Path
import unittest
from types import SimpleNamespace

APP = Path(__file__).resolve().parents[1] / 'backend/app.py'

def load_function(name, env):
    tree=ast.parse(APP.read_text(encoding='utf-8-sig'))
    fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name==name)
    fn.decorator_list=[]
    exec(compile(ast.Module(body=[fn],type_ignores=[]),str(APP),'exec'),env)
    return env[name]

class SortingIntegrationTests(unittest.TestCase):
    def test_capture_uses_enabled_camera_polling_channel(self):
        env={'CONFIG':{'imagesendermcu':{'command':None}}}
        load_function('request_sorting_capture',env)()
        self.assertEqual(env['CONFIG']['imagesendermcu']['command'],'capture')

    def test_offline_camera_cannot_use_cached_image(self):
        env={'request':SimpleNamespace(get_json=lambda **kwargs: {}),
             'get_device_status':lambda name:'Offline', 'jsonify':lambda value:value,
             'last_raw_image':b'old image'}
        response,code=load_function('start_sorting_sequence',env)()
        self.assertEqual(code,503)
        self.assertEqual(response['status'],'error')

    def test_progress_events_are_snapshots(self):
        from copy import deepcopy
        from datetime import datetime
        events=[]
        env={'sorting_status':{'step':'capturing'},'datetime':datetime,
             'deepcopy':deepcopy,'broadcast_event':events.append}
        load_function('broadcast_sorting_update',env)()
        env['sorting_status']['step']='inferring'
        self.assertEqual(events[0]['sorting']['step'],'capturing')

if __name__=='__main__': unittest.main()
