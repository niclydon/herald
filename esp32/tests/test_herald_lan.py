"""Exercise the real HTTPS handler against host fakes and ASAN."""
import pathlib
import subprocess
import tempfile
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class HeraldLan(unittest.TestCase):
    def test_admin_handlers(self):
        self.test_real_handlers(admin=1)
    def test_real_handlers(self, admin=0):
        cjson=ROOT/'managed_components/espressif__cjson/cJSON'
        with tempfile.TemporaryDirectory() as d:
            exe=pathlib.Path(d)/'lan'
            subprocess.run(['cc',f'-DCONFIG_HOMEHUB_LAN_ADMIN={admin}','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address','-I',str(ROOT/'tests/lan_fakes'),'-I',str(ROOT/'tests/remote_fakes'),'-I',str(ROOT/'main'),'-I',str(cjson),str(ROOT/'tests/herald_lan_harness.c'),str(cjson/'cJSON.c'),'-o',str(exe)],check=True,capture_output=True)
            result=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
            self.assertIn('completion ownership passed',result.stdout)
    def test_results_routed_before_muse_session_checks(self):
        s=(ROOT/'main/noise_control.cpp').read_text().split('static void queue_result(',1)[1]
        self.assertLess(s.index('HERALD_LAN_GENERATION'),s.index('!s_result_q'))
        self.assertIn('handle_invoke_request(json, strlen(json), HERALD_LAN_GENERATION)',s)

    def test_ota_accepts_authenticated_lan_recovery(self):
        s=(ROOT/'main/app.c').read_text().split('static void ota_verify_task(',1)[1].split('static ',1)[0]
        self.assertIn('herald_lan_has_authenticated_client()',s)
        self.assertIn('noise_ctrl_is_connected()',s)
