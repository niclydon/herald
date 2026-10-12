import { mkdirSync, existsSync, writeFileSync, chmodSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { randomBytes } from 'node:crypto';
import { execFileSync } from 'node:child_process';

export function provision(directory: string, host: string) {
  const dir = resolve(directory);
  if (existsSync(dir)) throw new Error('Provision into a new private directory; existing credentials will not be overwritten.');
  if (!/^[a-zA-Z0-9.:-]+$/.test(host)) throw new Error('Use a hostname or IP address, without scheme or port.');
  mkdirSync(dir, { recursive: true, mode: 0o700 });
  const cert = join(dir, 'herald-cert.pem'), key = join(dir, 'herald-key.pem');
  execFileSync('openssl', ['req','-x509','-newkey','ec','-pkeyopt','ec_paramgen_curve:prime256v1','-nodes','-days','3650',
    '-keyout',key,'-out',cert,'-subj','/CN=herald.local','-addext','subjectAltName=DNS:herald.local'], { stdio: 'pipe' });
  const token = randomBytes(32).toString('hex');
  const config = { url: `https://${host}:8443`, token, caFile: cert, servername: 'herald.local', admin: false };
  writeFileSync(join(dir,'client.json'), JSON.stringify(config,null,2)+'\n', { mode: 0o600 });
  writeFileSync(join(dir,'sdkconfig.lan'), [
    'CONFIG_HOMEHUB_LAN_CONTROL=y', 'CONFIG_HOMEHUB_LAN_ADMIN=n',
    `CONFIG_HOMEHUB_LAN_TOKEN="${token}"`, `CONFIG_HOMEHUB_LAN_CERT_FILE=${JSON.stringify(cert)}`,
    `CONFIG_HOMEHUB_LAN_KEY_FILE=${JSON.stringify(key)}`, ''
  ].join('\n'), { mode: 0o600 });
  chmodSync(key,0o600); chmodSync(cert,0o600);
  return { directory: dir, clientConfig: join(dir,'client.json'), overlay: join(dir,'sdkconfig.lan') };
}
