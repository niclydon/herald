import * as z from 'zod';
const percent=z.number().int().min(0).max(100);
const channel=z.number().int().min(0).max(255);
const volume={volume:percent.optional()};
const empty=z.object({}).strict();
const bleHandle=z.number().int().min(0).max(65534);
const bleUuid=z.string().regex(/^(?:[0-9a-fA-F]{4}|[0-9a-fA-F]{8}|[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$/);
const showPhase=z.object({
 colors:z.array(z.tuple([channel,channel,channel])).min(1).max(16),
 pattern:z.enum(['chase','alternate','fade','sparkle']),
 start_ms:z.number().int().min(0).max(86400000),
 duration_ms:z.number().int().min(1).max(86400000),
 speed_ms:z.number().int().min(20).max(60000).optional(),
 brightness:percent.optional(),
}).strict();
export const commands={
 'show.play':z.object({
  audio_url:z.url().refine(s=>s.startsWith('https://') && Buffer.byteLength(s)<=2048 && !/[\\\s]/.test(s) && !new URL(s).username && !new URL(s).password,'HTTPS URL without credentials, maximum 2048 bytes'),
  timeline:z.array(showPhase).min(1).max(32).refine(a=>a.every((p,i)=>i===0||p.start_ms>=a[i-1].start_ms),'Order phases by start_ms'),
  volume:percent.optional(),loop:z.boolean().optional(),
 }).strict(),
 'show.stop':empty,
 'ble.scan':z.object({duration_ms:z.number().int().min(1).max(60000).optional()}).strict(),
 'ble.connect':z.object({address:z.string().regex(/^(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$/),timeout_ms:z.number().int().min(100).max(60000).optional()}).strict(),
 'ble.discover':z.object({handle:bleHandle}).strict(),
 'ble.read':z.object({handle:bleHandle,characteristic_uuid:bleUuid}).strict(),
 'ble.write':z.object({handle:bleHandle,characteristic_uuid:bleUuid,value_hex:z.string().max(1024).regex(/^(?:[0-9a-fA-F]{2})*$/),with_response:z.boolean().optional()}).strict(),
 'ble.disconnect':z.object({handle:bleHandle}).strict(),
 'led.set':z.object({index:z.number().int().min(0).max(1),r:channel,g:channel,b:channel,brightness:percent.optional()}).strict(),
 'led.pattern':z.object({pattern:z.enum(['chase','alternate','fade','sparkle']),colors:z.array(z.tuple([channel,channel,channel])).min(1).max(16),speed_ms:z.number().int().min(20).max(60000).optional(),duration_ms:z.number().int().min(0).max(86400000).optional(),brightness:percent.optional()}).strict(),
 'led.reactive':z.object({mode:z.enum(['energy','spectrum','beat','duet','off']).optional(),colors:z.array(z.tuple([channel,channel,channel])).min(1).max(2).optional(),sensitivity:z.number().min(0.1).max(3).optional(),smoothing_ms:z.number().int().min(0).max(1000).optional()}).strict(),
 'led.clear':empty,'audio.stop':empty,'audio.test':z.object(volume).strict(),
 'audio.play':z.object({url:z.url().refine(s=>/^https?:\/\//.test(s)),...volume}).strict(),
 'audio.speak':z.object({text:z.string().min(1).refine(s=>Buffer.byteLength(s,'utf8')<=1000,'Maximum 1,000 UTF-8 bytes'),...volume}).strict(),
 'demo.show':z.object(volume).strict(),
 'mic.level':z.object({duration_ms:z.number().int().min(100).max(3000).optional()}).strict(), 'wifi.scan':empty,'wifi.status':empty,'wifi.reconnect':empty,
 'device.info':empty,'device.health':empty,'device.diagnostics':empty,'device.discover':empty,'device.reboot':empty,
 'device.ota':z.object({url:z.url().refine(s=>s.startsWith('https://'),'OTA requires HTTPS'),force:z.boolean().optional()}).strict(),
 'settings.get':empty,'settings.set':z.object({volume:percent.optional(),brightness:percent.optional()}).strict().refine(s=>s.volume!==undefined||s.brightness!==undefined,'Supply volume or brightness'),
} as const;
export type Command=keyof typeof commands;
export const admin=new Set(['device.ota','device.reboot','wifi.reconnect']);
export const reads=new Set(['device.info','device.health','device.diagnostics','device.discover','wifi.status','wifi.scan','settings.get','mic.level']);
export const descriptions:Record<Command,string>={
 'show.play':'Play HTTPS WAV and choreographed LED phases concurrently. Preloads audio before starting the shared clock. Latest active phase wins; gaps hold the last color. Volume defaults to 75. Returns a job; loop repeats until show.stop or audio.stop. Owns both engines.',
 'show.stop':'Stop the current show audio and freeze LEDs. Errors if no show is running. Poll the original job for cleanup completion.',
 'ble.scan':'Active nearby BLE scan, at most 16 peers sorted by RSSI. Requires central-enabled firmware. Poll the returned job.',
 'ble.connect':'Connect one BLE peripheral. Scan first for random address type; disconnect the current central session before another.',
 'ble.discover':'Enumerate services and characteristics for the open central handle. Returns a job.',
 'ble.read':'Read a unique characteristic UUID, up to 512 bytes as hex. Returns a job.',
 'ble.write':'Write at most 512 bytes as sequential MTU-3 chunks. Peripheral protocol must support reassembly; failure may leave partial remote state. No automatic retry.',
 'ble.disconnect':'Disconnect the central peripheral and free the session. Returns a job.',
 'led.set':'Set one of two external RGB LEDs. Stops the current pattern. Requires the LAN ownership lease.',
 'led.pattern':'Start chase, alternate, fade or sparkle on both RGB LEDs. Returns a job ID immediately. duration_ms=0 runs until replaced or cancelled.',
 'led.reactive':'React to outgoing speaker PCM; duet is approximate frequency weighting, not voice separation. Runs until replaced or mode off.',
 'led.clear':'Stop the current pattern and turn both external LEDs off.',
 'audio.stop':'Request playback cancellation; pending TTS may finish downloading but will not play.',
 'audio.test':'Play a short test tone. Start with volume 10 for wiring checks.',
 'audio.play':'Play a reachable PCM16 WAV (8-48kHz, mono/stereo, <=2MiB). Returns a job ID.',
 'audio.speak':'Speak text through the configured TTS service. Maximum 1,000 UTF-8 bytes; saved volume applies unless overridden. Returns a job ID.',
 'mic.level':'Measure sound level with the ZTS6631 microphone: short capture returning rms, peak and dbfs. Level metering only; audio is discarded. Returns a job.',
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
