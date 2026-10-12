import pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
CJSON=ROOT/'managed_components/espressif__cjson/cJSON'
class Reactive(unittest.TestCase):
    def test_dsp(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe=pathlib.Path(tmp)/'reactive'
            subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I',str(ROOT/'main'),'-I',str(CJSON),str(ROOT/'main/led_reactive_dsp.c'),str(ROOT/'tests/reactive_dsp_harness.c'),str(CJSON/'cJSON.c'),'-lm','-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_actual_worker_and_tap(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe=pathlib.Path(tmp)/'worker'
            subprocess.run(['cc','-DCONFIG_HOMEHUB_SHOW=1','-DCONFIG_HOMEHUB_LED_REACTIVE=1','-DportMAX_DELAY=0xffffffffU','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I',str(ROOT/'tests/reactive_fakes'),'-I',str(ROOT/'tests/remote_fakes'),'-I',str(ROOT/'main'),'-I',str(CJSON),*[str(ROOT/'main'/name) for name in ['remote_led.c','led_reactive.c','led_reactive_dsp.c']],str(ROOT/'tests/remote_led_harness.c'),str(CJSON/'cJSON.c'),'-lm','-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
