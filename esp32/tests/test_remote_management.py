"""Compile the real LED lifecycle and real registration serializer on the host."""
import json
import pathlib
import subprocess
import tempfile
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
CJSON=ROOT/'managed_components/espressif__cjson/cJSON'
class RemoteManagement(unittest.TestCase):
    def test_pattern_worker_lifecycle(self):
        with tempfile.TemporaryDirectory() as temp:
            exe=pathlib.Path(temp)/'led'
            subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address','-I',str(ROOT/'tests/remote_fakes'),'-I',str(ROOT/'main'),'-I',str(CJSON),str(ROOT/'main/remote_led.c'),str(ROOT/'tests/remote_led_harness.c'),str(CJSON/'cJSON.c'),'-lm','-o',str(exe)],check=True,capture_output=True)
            out=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
            self.assertIn('200 completions passed; one worker task',out.stdout)
    def test_real_registration_fits_and_preserves_commands(self):
        source=(ROOT/'main/noise_control.cpp').read_text()
        code=source[source.index('static cJSON *string_param('):source.index('// ---- OTA callback')]
        prefix='''#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cJSON.h"
#define CONFIG_HOMEHUB_REMOTE_MANAGEMENT 1
#define CONFIG_HOMEHUB_REMOTE_LEDS 1
#define CONFIG_HOMEHUB_I2S_SPEAKER 1
#define CONFIG_HOMEHUB_EXTERNAL_RGB_TEST 1
#define CONFIG_HOMEHUB_TUNNEL 1
#define CONFIG_HOMEHUB_SHORT_NAME "Hal"
static char s_wifi_ssid[33],s_node_id[]="homelink-000001",s_display_name[]="Herald",s_register_req_id[40];
static void copy_wifi_ssid(char *out,size_t n) { snprintf(out,n,"01234567890123456789012345678901"); }
static void make_uuid(char *out,size_t n) { snprintf(out,n,"01234567-0123-0123-0123-012345678901"); }
struct esp_app_desc_t { const char *version; };
static const esp_app_desc_t *esp_app_get_description() { static esp_app_desc_t d={"999.0.1"}; return &d; }
static bool ota_is_enabled() { return true; }
'''
        with tempfile.TemporaryDirectory() as temp:
            temp=pathlib.Path(temp); src=temp/'register.cpp'; exe=temp/'register'
            src.write_text(prefix+code+'\nint main() { char *p=build_register_json(); if(!p) return 1; puts(p); free(p); }\n')
            obj=temp/'cjson.o'
            subprocess.run(['cc','-I',str(CJSON),'-c',str(CJSON/'cJSON.c'),'-o',str(obj)],check=True,capture_output=True)
            subprocess.run(['c++','-std=c++17','-I',str(CJSON),str(src),str(obj),'-o',str(exe)],check=True,capture_output=True)
            raw=subprocess.run([str(exe)],check=True,capture_output=True,text=True).stdout.strip()
            self.assertLess(len(raw.encode()),8192)
            data=json.loads(raw); commands=data['params']['commands_v2']
            self.assertEqual(data['params']['display_name'],'Herald')
            for command in ('led.set','led.pattern','led.clear','audio.stop','wifi.scan','wifi.status','wifi.reconnect','device.reboot','device.info','device.diagnostics','settings.get','settings.set','demo.show','audio.test','audio.play','audio.speak','device.health','device.ota','device.discover'):
                self.assertIn(command,commands)
            print(f'Herald full registration: {len(raw.encode())} bytes')
    def test_new_advertisements_match_dispatch_gates(self):
        expected={**dict.fromkeys(('led.set','led.pattern','led.clear'),'CONFIG_HOMEHUB_REMOTE_LEDS'),**dict.fromkeys(('wifi.scan','wifi.status','wifi.reconnect','device.reboot','device.info','device.diagnostics','settings.get','settings.set','audio.stop'),'CONFIG_HOMEHUB_REMOTE_MANAGEMENT')}
        for filename in ('app.c','noise_control.cpp'):
            guards=[]; found=set()
            for line in (ROOT/'main'/filename).read_text().splitlines():
                text=line.strip()
                if text.startswith(('#if ','#ifdef ','#ifndef ')): guards.append(text)
                elif text.startswith('#endif'):
                    if guards: guards.pop()
                for name,gate in expected.items():
                    if f'"{name}"' in line:
                        found.add(name); self.assertTrue(any(gate in g for g in guards),(filename,name,guards))
            self.assertEqual(found,set(expected))
if __name__=='__main__': unittest.main()
