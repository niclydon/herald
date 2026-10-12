"""Real show coordinator, concurrent audio task, and deterministic timeline tests."""
import pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
CJSON=ROOT/'managed_components/espressif__cjson/cJSON'
class Show(unittest.TestCase):
    def test_concurrent_coordinator(self):
        with tempfile.TemporaryDirectory() as d:
            exe=pathlib.Path(d)/'show'
            args=['cc','-std=c11','-D_DARWIN_C_SOURCE','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address','-pthread','-I',str(ROOT/'tests/show_fakes'),'-I',str(ROOT/'main'),'-I',str(CJSON),str(ROOT/'main/show.c'),str(ROOT/'tests/show_harness.c'),str(CJSON/'cJSON.c'),'-lm','-o',str(exe)]
            r=subprocess.run(args,capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stderr);self.assertIn('exactly-once results passed',r.stdout)
    def test_command_gates_and_audio_stop(self):
        for filename in ['app.c','noise_control.cpp']:
            guards=[];found=set()
            for line in (ROOT/'main'/filename).read_text().splitlines():
                t=line.strip()
                if t.startswith(('#if ','#ifdef ','#ifndef ')):guards.append(t)
                elif t.startswith('#endif') and guards:guards.pop()
                for name in ['show.play','show.stop']:
                    if '"'+name+'"' in line:
                        found.add(name);self.assertTrue(any('CONFIG_HOMEHUB_SHOW' in x for x in guards))
            self.assertEqual(found,{'show.play','show.stop'})
        speaker=(ROOT/'main/speaker.c').read_text();stop=speaker[speaker.index('cJSON *speaker_stop_command'):speaker.index('cJSON *speaker_command')];self.assertIn('show_request_stop()',stop)
