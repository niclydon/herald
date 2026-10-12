"""Exercise WAV boundary/format checks against real C parser."""
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

def wav(channels=1, rate=16000, bits=16, fmt=1, samples=b'\0\0'*32):
    align = channels * bits // 8
    body = b'fmt ' + struct.pack('<IHHIIHH',16,fmt,channels,rate,rate*align,align,bits)
    body += b'data' + struct.pack('<I',len(samples)) + samples
    return b'RIFF'+struct.pack('<I',len(body)+4)+b'WAVE'+body

class SpeakerWav(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory()
        cls.exe=pathlib.Path(cls.tmp.name)/'parser'
        src=pathlib.Path(cls.tmp.name)/'parser.c'
        src.write_text('''#include "speaker_wav.h"
#include <stdio.h>
int main(void) { uint8_t b[4096]; speaker_wav_t w;
size_t n=fread(b,1,sizeof b,stdin);
if(!speaker_parse_wav(b,n,&w)) return 1;
printf("%u %u %zu",w.rate,w.channels,w.bytes); return 0; }
''')
        subprocess.run(['cc','-Wall','-Wextra','-Werror','-I',str(ROOT/'main'),str(src),'-o',str(cls.exe)],check=True)
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def parse(self, data): return subprocess.run([str(self.exe)],input=data,capture_output=True)
    def test_supported_mono_and_stereo(self):
        for ch in (1,2):
            for rate in (8000,16000,44100,48000):
                r=self.parse(wav(channels=ch,rate=rate))
                self.assertEqual(r.returncode,0)
                self.assertEqual(r.stdout.decode(),f'{rate} {ch} 64')
    def test_truncation_at_every_byte(self):
        data=wav()
        for n in range(len(data)): self.assertNotEqual(self.parse(data[:n]).returncode,0)
    def test_reject_unsupported_audio(self):
        for kwargs in ({'fmt':3},{'bits':8},{'channels':3},{'rate':96000},{'samples':b'\0'},{'samples':b''}):
            self.assertNotEqual(self.parse(wav(**kwargs)).returncode,0)
    def test_chunk_length_overflow(self):
        b=bytearray(wav()); b[16:20]=b'\xff'*4
        self.assertNotEqual(self.parse(b).returncode,0)
    def test_registration_and_dispatch_are_gated(self):
        for file in ('app.c','noise_control.cpp'):
            text=(ROOT/'main'/file).read_text()
            block=text.split('#if CONFIG_HOMEHUB_I2S_SPEAKER',1)[1] if file=='noise_control.cpp' else text.split('static cJSON *on_ws_command(',1)[1].split('#if CONFIG_HOMEHUB_I2S_SPEAKER',1)[1]
            block=block.split('#endif',1)[0]
            self.assertIn('audio.test',block); self.assertIn('audio.play',block)
            self.assertIn('audio.speak',block)
    def test_show_registration_and_dispatch_require_both_peripherals(self):
        for filename in ('app.c', 'noise_control.cpp'):
            guards=[]; matches=0
            for line in (ROOT/'main'/filename).read_text().splitlines():
                directive=line.strip()
                if directive.startswith(('#if ', '#ifdef ', '#ifndef ')):
                    guards.append(directive)
                elif directive.startswith('#endif'):
                    if guards: guards.pop()
                if '"demo.show"' in line:
                    matches+=1
                    self.assertTrue(any('CONFIG_HOMEHUB_I2S_SPEAKER' in g for g in guards))
                    self.assertTrue(any('CONFIG_HOMEHUB_EXTERNAL_RGB_TEST' in g for g in guards))
            self.assertGreater(matches,0)

if __name__=='__main__': unittest.main()
