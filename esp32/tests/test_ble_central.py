"""Real central worker compiled against deterministic NimBLE/RTOS fakes."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
CJSON=ROOT/'managed_components/espressif__cjson/cJSON'
class BleCentral(unittest.TestCase):
    def test_worker_validation_and_lifecycle(self):
        with tempfile.TemporaryDirectory() as d:
            exe=pathlib.Path(d)/'ble'
            cmd=['cc','-std=c11','-D_DARWIN_C_SOURCE','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address','-I',str(ROOT/'tests/ble_fakes'),'-I',str(ROOT/'main'),'-I',str(CJSON),str(ROOT/'tests/ble_central_harness.c'),str(CJSON/'cJSON.c'),'-lm','-o',str(exe)]
            p=subprocess.run(cmd,capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)
            p=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)
            self.assertIn('late callback cleanup passed',p.stdout)
    def test_advertisement_dispatch_and_host_lifetime(self):
        for filename in ['app.c','noise_control.cpp']:
            guards=[];found=set()
            for line in (ROOT/'main'/filename).read_text().splitlines():
                t=line.strip()
                if t.startswith(('#if ','#ifdef ','#ifndef ')):guards.append(t)
                elif t.startswith('#endif') and guards:guards.pop()
                for name in ['ble.scan','ble.connect','ble.discover','ble.read','ble.write','ble.disconnect']:
                    if '"'+name+'"' in line:
                        found.add(name);self.assertTrue(any('CONFIG_HOMEHUB_BLE_CENTRAL' in x for x in guards))
            self.assertEqual(len(found),6)
        app=(ROOT/'main/app.c').read_text()
        self.assertEqual(app.count('#if CONFIG_MUSE_ENABLED || CONFIG_HOMEHUB_BLE_CENTRAL'),2)
        self.assertIn('|| CONFIG_HOMEHUB_BLE_CENTRAL\n        // Muse may',app)
