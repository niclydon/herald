import { readFileSync, statSync } from 'node:fs';
import { Agent, request } from 'node:https';
import { randomUUID } from 'node:crypto';
export interface Config { url: string; token: string; caFile: string; servername: string; admin?: boolean }
export interface Job { id: string; state: 'running'|'done'; result?: { ok: boolean; payload?: unknown; error?: { code?: string; message?: string } } }
export class HeraldError extends Error { constructor(message: string, public status?: number) { super(message); } }
export function loadConfig(path = process.env.HERALD_CONFIG): Config {
  if (!path) throw new HeraldError('Set HERALD_CONFIG to your private client.json. Run herald provision DIRECTORY HOST first.');
  if (process.platform !== 'win32' && (statSync(path).mode & 0o077)) throw new HeraldError('Client config must be private: chmod 600 the file.');
  const c = JSON.parse(readFileSync(path,'utf8')) as Config;
  if (!c.url?.startsWith('https://') || typeof c.token!=='string' || c.token.length<32 || !c.caFile || !c.servername)
    throw new HeraldError('Config requires an HTTPS URL, token >=32 characters, caFile and servername.');
  const u=new URL(c.url); if(u.username||u.password||u.search||u.hash) throw new HeraldError('Do not place credentials or query parameters in the device URL.');
  return c;
}
export class HeraldClient {
  readonly owner: string;
  private agent = new Agent({keepAlive:true,maxSockets:1});
  private ca: Buffer;
  constructor(readonly config: Config, owner: string = randomUUID()) {
    if(!/^[A-Za-z0-9_-]{1,64}$/.test(owner)) throw new HeraldError('Owner must be 1-64 letters, numbers, underscores or hyphens.');
    this.owner=owner; this.ca=readFileSync(config.caFile);
  }
  close() { this.agent.destroy(); }
  async request<T>(method: string, path: string, data?: unknown): Promise<T> {
    const body=data===undefined?'':JSON.stringify(data);
    return await new Promise<T>((resolve,reject)=>{
      const req=request(new URL(path,this.config.url),{method,agent:this.agent,ca:this.ca,servername:this.config.servername,
        headers:{Authorization:`Bearer ${this.config.token}`,'Content-Type':'application/json','X-Herald-Owner':this.owner,...(body?{'Content-Length':Buffer.byteLength(body)}:{})}},res=>{
        const chunks:Buffer[]=[];let size=0;
        res.on('data',(chunk:Buffer)=>{size+=chunk.length;if(size>256*1024) {res.destroy();reject(new HeraldError('Device response exceeded 256 KiB.'));} else chunks.push(chunk);});
        res.on('error',()=>reject(new HeraldError('Device response interrupted; query the job ID before retrying.')));
        res.on('end',()=>{
          try { const out=JSON.parse(Buffer.concat(chunks).toString());
            if((res.statusCode??500)>=400) reject(new HeraldError(String(out.error??'device_error'),res.statusCode)); else resolve(out as T);
          } catch {reject(new HeraldError('Device returned malformed JSON.'));}
        });
      });
      req.setTimeout(15000,()=>req.destroy(new Error('timeout')));
      req.on('error',(e:NodeJS.ErrnoException)=>reject(new HeraldError(`Device connection failed (${e.code??'timeout'}). Check address, power and trusted certificate; no automatic command retry was made.`)));
      req.end(body);
    });
  }
  describe() { return this.request<{name:string;protocol:number;commands:string[];admin_enabled:boolean}>('GET','/v1/describe'); }
  status(id:string) {
    if(!/^[A-Za-z0-9_-]{1,64}$/.test(id)) throw new HeraldError('Invalid job ID.');
    return this.request<Job>('GET',`/v1/jobs/${id}`);
  }
  release() { return this.request('POST','/v1/release',{owner:this.owner}); }
  async submit(command:string,params:Record<string,unknown>={},id: string=randomUUID()):Promise<Job> {
    if(['device.ota','device.reboot','wifi.reconnect'].includes(command)&&!this.config.admin) throw new HeraldError('Administrative commands are disabled in this client configuration.');
    try { return await this.request<Job>('POST','/v1/commands',{id,owner:this.owner,command,params}); }
    catch(e) { if(e instanceof HeraldError) e.message+=` Job ID: ${id}.`; throw e; }
  }
  async wait(id:string,timeoutMs=30000,signal?:AbortSignal):Promise<Job> {
    const deadline=Date.now()+timeoutMs;
    while(Date.now()<deadline) {
      if(signal?.aborted) throw new HeraldError(`Waiting cancelled; job ${id} may still be running. Use herald_cancel to stop outputs.`);
      const job=await this.status(id); if(job.state==='done') return job;
      await new Promise(r=>setTimeout(r,250));
    }
    return {id,state:'running'};
  }
  async invoke(command:string,params:Record<string,unknown>={},waitMs=30000):Promise<Job> {
    const job=await this.submit(command,params);
    return job.state==='done'||waitMs===0?job:this.wait(job.id,waitMs);
  }
  async notify(phase:'working'|'success'|'failure',text?:string) {
    const colors=phase==='working'?[[0,0,255]]:phase==='success'?[[0,255,0]]:[[255,0,0]];
    const lights=await this.submit('led.pattern',{pattern:phase==='failure'?'sparkle':'fade',colors,speed_ms:1000,duration_ms:phase==='working'?0:4000,brightness:65});
    const speech=text?await this.submit('audio.speak',{text}):undefined;
    return {lights,...(speech?{speech}:{})};
  }
  async cancel() {
    const audio=await this.invoke('audio.stop'); const leds=await this.invoke('led.clear');
    if(audio.result?.ok&&leds.result?.ok) return {audio,leds,released:await this.release()};
    throw new HeraldError('Cancellation was not fully applied; inspect audio/LED state and retry.');
  }
}
