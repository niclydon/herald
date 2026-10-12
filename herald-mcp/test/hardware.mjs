// Explicit opt-in hardware verification: runs LEDs and speaks one sentence.
import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { randomUUID } from 'node:crypto';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { HeraldClient,loadConfig } from '../dist/client.js';
const config=loadConfig();const owner='hardware-test';
const direct=new HeraldClient(config,owner),other=new HeraldClient(config,'competing-agent');
const bad=new HeraldClient({...config,token:'wrong-token-for-authentication-check'});
const mcp=new Client({name:'herald-hardware-verifier',version:'1.0.0'});
const transport=new StdioClientTransport({command:process.execPath,args:[resolve('dist/server.js')],env:{HERALD_CONFIG:process.env.HERALD_CONFIG,HERALD_OWNER:owner}});
try {
 await assert.rejects(bad.describe(),e=>e.status===401);
 const info=await direct.invoke('device.info');assert(info.result.ok);console.log('Firmware:',info.result.payload.firmware_version);
 await mcp.connect(transport);
 const tools=await mcp.listTools();assert(tools.tools.some(t=>t.name==='herald_audio_speak'));
 const pattern=await mcp.callTool({name:'herald_led_pattern',arguments:{pattern:'fade',colors:[[0,0,255],[0,255,0]],speed_ms:500,duration_ms:4000,brightness:75}});
 assert.equal(pattern.isError,false);const job=pattern.structuredContent.data;
 await assert.rejects(other.submit('led.clear'),e=>e.status===409);
 const completed=await direct.wait(job.id,15000);assert.equal(completed.result.ok,true);
 console.log('MCP LED pattern completed; competing owner rejected.');
 const id=randomUUID();const a=await direct.submit('settings.get',{},id),b=await direct.submit('settings.get',{},id);
 assert.equal(a.id,b.id);assert.equal(b.state,'done');
 await assert.rejects(direct.submit('device.info',{},id),e=>e.status===409);
 console.log('Duplicate request replay and changed-payload rejection passed.');
 const speech=await mcp.callTool({name:'herald_audio_speak',arguments:{text:'Herald is ready. Codex and other agents can now control my lights and speaker over an encrypted local connection.',volume:75}});
 assert.equal(speech.isError,false);const speechJob=speech.structuredContent.data;
 const spoken=await direct.wait(speechJob.id,270000);assert.equal(spoken.state,'done');assert.equal(spoken.result.ok,true);
 console.log('MCP speech completed.');
 const stop=await mcp.callTool({name:'herald_cancel',arguments:{}});assert.equal(stop.isError,false);
 console.log('Cancellation and ownership release completed.');
}finally{await mcp.close();direct.close();other.close();bad.close();}
