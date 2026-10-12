import { test,before,after } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync,readFileSync,writeFileSync,rmSync,chmodSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join,resolve } from 'node:path';
import { createServer } from 'node:https';
import { HeraldClient,loadConfig } from '../dist/client.js';
import { provision } from '../dist/provision.js';
import { commands } from '../dist/schemas.js';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
let dir,config,file,http,client;let calls=[];
before(async()=>{
 dir=mkdtempSync(join(tmpdir(),'herald-test-'));provision(join(dir,'private'),'127.0.0.1');
 file=join(dir,'private/client.json');config=loadConfig(file);
 http=createServer({cert:readFileSync(config.caFile),key:readFileSync(join(dir,'private/herald-key.pem'))},async(req,res)=>{
  res.setHeader('Content-Type','application/json');
  if(req.headers.authorization!==`Bearer ${config.token}`){res.writeHead(401).end('{"error":"unauthorized"}');return;}
  if(req.url==='/v1/describe'){res.end(JSON.stringify({name:'Herald',protocol:1,commands:Object.keys(commands),admin_enabled:false}));return;}
  const chunks=[];for await(const chunk of req)chunks.push(chunk);
  const body=chunks.length?JSON.parse(Buffer.concat(chunks)):{};calls.push(body);
  if(req.url==='/v1/commands')res.end(JSON.stringify({id:body.id,state:'done',result:{ok:true,payload:{firmware_version:'test',volume:75}}}));
  else res.end(JSON.stringify({released:true}));
 });
 await new Promise(r=>http.listen(0,'127.0.0.1',r));config.url=`https://127.0.0.1:${http.address().port}`;
 writeFileSync(file,JSON.stringify(config));client=new HeraldClient(config,'test-owner');
});
after(async()=>{client.close();http.closeAllConnections();await new Promise(r=>http.close(r));rmSync(dir,{recursive:true,force:true});});
test('authenticated HTTPS uses private CA and explicit server identity',async()=>{assert.equal((await client.describe()).name,'Herald');assert.equal((await client.invoke('device.info')).result.ok,true);});
test('incorrect token fails without exposing it',async()=>{const c=new HeraldClient({...config,token:'invalid-token-never-print-this-value'});try{await assert.rejects(c.describe(),e=>e.status===401&&!e.message.includes('invalid-token'));}finally{c.close();}});
test('incorrect TLS hostname is rejected',async()=>{const c=new HeraldClient({...config,servername:'wrong.local'});try{await assert.rejects(c.describe(),/connection failed/);}finally{c.close();}});
test('administrative commands blocked in library by default',async()=>{await assert.rejects(client.submit('device.reboot'),/disabled/);});
test('UTF-8 length, RGB ranges and unknown parameters validated',()=>{assert.throws(()=>commands['audio.speak'].parse({text:'🙂'.repeat(251)}));assert.throws(()=>commands['led.set'].parse({index:2,r:0,g:0,b:0}));assert.throws(()=>commands['settings.set'].parse({}));assert.throws(()=>commands['led.clear'].parse({surprise:1}));});
test('configuration permissions enforced',()=>{chmodSync(file,0o644);try{assert.throws(()=>loadConfig(file),/chmod/);}finally{chmodSync(file,0o600);}});
test('real MCP stdio lists typed tools and invokes shared client',async()=>{
 const transport=new StdioClientTransport({command:process.execPath,args:[resolve('dist/server.js')],env:{HERALD_CONFIG:file,HERALD_OWNER:'mcp-test'}});
 const mcp=new Client({name:'herald-integration-test',version:'1.0.0'});
 try{
  await mcp.connect(transport);
  const tools=await mcp.listTools();
  assert(tools.tools.some(t=>t.name==='herald_led_pattern'));
  assert(!tools.tools.some(t=>t.name==='herald_device_reboot'));
  const result=await mcp.callTool({name:'herald_device_info',arguments:{}});
  assert.equal(result.isError,false);assert.equal(result.structuredContent.data.result.ok,true);
  const rejected=await mcp.callTool({name:'herald_led_set',arguments:{index:9,r:1,g:2,b:3}});
  assert.equal(rejected.isError,true);
  assert(calls.some(c=>c.owner==='mcp-test'&&c.command==='device.info'));
 }finally{await mcp.close();}
});

test('authenticated Streamable HTTP serves MCP and rejects bad auth and origins',async()=>{
 const {spawn}=await import('node:child_process');const {request}=await import('node:https');const net=await import('node:net');
 const portProbe=net.createServer();await new Promise(r=>portProbe.listen(0,'127.0.0.1',r));const port=portProbe.address().port;await new Promise(r=>portProbe.close(r));
 const token='synthetic-http-test-token-with-32-characters';
 const httpFile=join(dir,'http.json');writeFileSync(httpFile,JSON.stringify({host:'127.0.0.1',port,certFile:config.caFile,keyFile:join(dir,'private/herald-key.pem'),allowedHosts:['127.0.0.1'],clients:{tester:{token,admin:false}}}),{mode:0o600});
 const child=spawn(process.execPath,['dist/server.js','--http'],{env:{...process.env,HERALD_CONFIG:file,HERALD_HTTP_CONFIG:httpFile},stdio:['ignore','ignore','pipe']});
 try{
  await new Promise((r,j)=>{const t=setTimeout(()=>j(new Error('HTTP bridge startup timeout')),5000);child.stderr.on('data',b=>{if(b.toString().includes('listening')){clearTimeout(t);r();}});child.on('exit',c=>{clearTimeout(t);j(new Error('HTTP bridge exited '+c));});});
  const send=(body,auth=token,origin)=>new Promise((resolve,reject)=>{
   const text=JSON.stringify(body);const req=request(`https://127.0.0.1:${port}/mcp`,{method:'POST',ca:readFileSync(config.caFile),servername:'herald.local',headers:{Authorization:`Bearer ${auth}`,'Content-Type':'application/json',Accept:'application/json, text/event-stream','Content-Length':Buffer.byteLength(text),...(origin?{Origin:origin}:{})}},res=>{const chunks=[];res.on('data',b=>chunks.push(b));res.on('end',()=>resolve({status:res.statusCode,text:Buffer.concat(chunks).toString()}));});req.on('error',reject);req.end(text);
  });
  const msg={jsonrpc:'2.0',id:1,method:'initialize',params:{protocolVersion:'2025-11-25',capabilities:{},clientInfo:{name:'test',version:'1'}}};
  assert.equal((await send(msg,'wrong')).status,401);
  assert.equal((await send(msg,token,'https://attacker.invalid')).status,403);
  const init=await send(msg);assert.equal(init.status,200,init.text);
  const list=await send({jsonrpc:'2.0',id:2,method:'tools/list'});assert.equal(list.status,200,list.text);
  const tools=JSON.parse(list.text).result.tools;assert(tools.some(t=>t.name==='herald_device_info'));assert(!tools.some(t=>t.name==='herald_device_ota'));
  const call=await send({jsonrpc:'2.0',id:3,method:'tools/call',params:{name:'herald_device_info',arguments:{}}});assert.equal(call.status,200,call.text);assert.equal(JSON.parse(call.text).result.isError,false);
 }finally{child.kill();await new Promise(r=>child.once('exit',r));}
});

test('BLE schemas validate MAC, UUID, hex, handles and scan bounds',()=>{
 assert.throws(()=>commands['ble.connect'].parse({address:'bad'}));
 assert.throws(()=>commands['ble.scan'].parse({duration_ms:60001}));
 assert.throws(()=>commands['ble.read'].parse({handle:-1,characteristic_uuid:'2a19'}));
 assert.throws(()=>commands['ble.read'].parse({handle:1,characteristic_uuid:'not-a-uuid'}));
 assert.throws(()=>commands['ble.write'].parse({handle:1,characteristic_uuid:'2a19',value_hex:'abc'}));
 assert.throws(()=>commands['ble.write'].parse({handle:1,characteristic_uuid:'2a19',value_hex:'zz'}));
 assert.throws(()=>commands['ble.write'].parse({handle:1,characteristic_uuid:'2a19',value_hex:'00'.repeat(513)}));
 assert.equal(commands['ble.write'].parse({handle:1,characteristic_uuid:'2a19',value_hex:'aabb',with_response:true}).with_response,true);
});

test('show timeline schema enforces timing, color, ordering, and HTTPS limits', async()=>{
 const {commands}=await import('../dist/schemas.js');
 const phase={pattern:'fade',colors:[[255,0,0]],start_ms:0,duration_ms:100};
 const valid={audio_url:'https://example.com/show.wav',timeline:[phase]};
 assert.equal(commands['show.play'].safeParse(valid).success,true);
 for(const bad of [
  {...valid,audio_url:'http://example.com/show.wav'},
  {...valid,audio_url:'https://user:pass@example.com/show.wav'},
  {...valid,volume:101}, {...valid,loop:1}, {...valid,timeline:[]},
  {...valid,timeline:[{...phase,duration_ms:0}]},
  {...valid,timeline:[{...phase,colors:[[256,0,0]]}]},
  {...valid,timeline:[{...phase,speed_ms:19}]},
  {...valid,timeline:[{...phase,start_ms:10},phase]},
 ]) assert.equal(commands['show.play'].safeParse(bad).success,false);
 assert.equal(commands['show.stop'].safeParse({}).success,true);
 assert.equal(commands['show.stop'].safeParse({extra:true}).success,false);
});
test('admin MCP exposes OTA and dispatches through the authenticated client',async()=>{
 const adminFile=join(dir,'private/admin-client.json');
 writeFileSync(adminFile,JSON.stringify({...config,admin:true}),{mode:0o600});
 const transport=new StdioClientTransport({command:process.execPath,args:[resolve('dist/server.js')],env:{HERALD_CONFIG:adminFile,HERALD_OWNER:'ota-test'}});
 const mcp=new Client({name:'herald-ota-test',version:'1.0.0'});
 try{
  await mcp.connect(transport);
  assert((await mcp.listTools()).tools.some(t=>t.name==='herald_device_ota'));
  const result=await mcp.callTool({name:'herald_device_ota',arguments:{url:'https://example.com/firmware.bin'}});
  assert.equal(result.isError,false);
  assert(calls.some(c=>c.owner==='ota-test'&&c.command==='device.ota'&&c.params.url==='https://example.com/firmware.bin'));
 }finally{await mcp.close();}
});

test('reactive schema accepts modes and rejects invalid ranges and colors',()=>{
 for(const mode of ['energy','spectrum','beat','duet','off']) assert.equal(commands['led.reactive'].safeParse({mode}).success,true);
 assert.equal(commands['led.reactive'].safeParse({sensitivity:0.1,smoothing_ms:0,colors:[[0,255,10]]}).success,true);
 for(const params of [{mode:'unknown'},{sensitivity:0},{smoothing_ms:1.5},{colors:[]},{colors:[[1,2,256]]},{unknown:1}]) assert.equal(commands['led.reactive'].safeParse(params).success,false);
});
