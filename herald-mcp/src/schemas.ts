import * as z from 'zod';
const percent=z.number().int().min(0).max(100);
const channel=z.number().int().min(0).max(255);
const volume={volume:percent.optional()};
const empty=z.object({}).strict();
export const commands={
 'led.set':z.object({index:z.number().int().min(0).max(1),r:channel,g:channel,b:channel,brightness:percent.optional()}).strict(),
 'led.pattern':z.object({pattern:z.enum(['chase','alternate','fade','sparkle']),colors:z.array(z.tuple([channel,channel,channel])).min(1).max(16),speed_ms:z.number().int().min(20).max(60000).optional(),duration_ms:z.number().int().min(0).max(86400000).optional(),brightness:percent.optional()}).strict(),
 'led.clear':empty,'audio.stop':empty,'audio.test':z.object(volume).strict(),
 'audio.play':z.object({url:z.url().refine(s=>/^https?:\/\//.test(s)),...volume}).strict(),
 'audio.speak':z.object({text:z.string().min(1).refine(s=>Buffer.byteLength(s,'utf8')<=1000,'Maximum 1,000 UTF-8 bytes'),...volume}).strict(),
 'demo.show':z.object(volume).strict(), 'wifi.scan':empty,'wifi.status':empty,'wifi.reconnect':empty,
 'device.info':empty,'device.health':empty,'device.diagnostics':empty,'device.discover':empty,'device.reboot':empty,
 'device.ota':z.object({url:z.url().refine(s=>s.startsWith('https://'),'OTA requires HTTPS'),force:z.boolean().optional()}).strict(),
 'settings.get':empty,'settings.set':z.object({volume:percent.optional(),brightness:percent.optional()}).strict().refine(s=>s.volume!==undefined||s.brightness!==undefined,'Supply volume or brightness'),
} as const;
export type Command=keyof typeof commands;
export const admin=new Set(['device.ota','device.reboot','wifi.reconnect']);
export const reads=new Set(['device.info','device.health','device.diagnostics','device.discover','wifi.status','wifi.scan','settings.get']);
export const descriptions:Record<Command,string>={
 'led.set':'Set one of two external RGB LEDs. Stops the current pattern. Requires the LAN ownership lease.',
 'led.pattern':'Start chase, alternate, fade or sparkle on both RGB LEDs. Returns a job ID immediately. duration_ms=0 runs until replaced or cancelled.',
 'led.clear':'Stop the current pattern and turn both external LEDs off.',
 'audio.stop':'Request playback cancellation; pending TTS may finish downloading but will not play.',
 'audio.test':'Play a short test tone. Start with volume 10 for wiring checks.',
 'audio.play':'Play a reachable PCM16 WAV (8-48kHz, mono/stereo, <=2MiB). Returns a job ID.',
 'audio.speak':'Speak text through the configured TTS service. Maximum 1,000 UTF-8 bytes; saved volume applies unless overridden. Returns a job ID.',
 'demo.show':'Run a narrated two-LED and speaker showcase after TTS preparation and a 20-second countdown. Returns a job ID.',
 'wifi.scan':'Actively scan visible Wi-Fi networks. Runs asynchronously.', 'wifi.status':'Read network, signal strength, reconnect count and uptime.',
 'wifi.reconnect':'ADMIN: disconnect and rejoin the stored network. Polling may disconnect; verify wifi.status afterward.',
 'device.info':'Read hardware, firmware, memory and device identity.', 'device.health':'Read existing Link health information.',
 'device.diagnostics':'Read reset reason, heap, OTA state and bounded filtered warning/error log tail.',
 'device.discover':'Discover devices on the gadget LAN using the existing discovery implementation.',
 'device.reboot':'ADMIN: reboot after acknowledgement; the HTTPS connection will close.',
 'device.ota':'ADMIN: install a newer privately hosted signed firmware image. Reboots on success; verify device.info after reconnecting.',
 'settings.get':'Read persisted volume and brightness defaults.', 'settings.set':'Persist volume and/or brightness defaults (0-100).',
};
