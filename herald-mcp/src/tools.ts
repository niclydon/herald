import { McpServer } from '@modelcontextprotocol/server';
import * as z from 'zod';
import { HeraldClient } from './client.js';
import { commands, admin, reads, descriptions, type Command } from './schemas.js';
export function createServer(client:HeraldClient) {
  const server=new McpServer({name:'herald-mcp-server',version:'0.1.0'});
  const output=(data:unknown,isError=false)=>({content:[{type:'text' as const,text:JSON.stringify(data)}],structuredContent:{data},isError});
  for(const [name,schema] of Object.entries(commands)) {
    if(admin.has(name)&&!client.config.admin) continue;
    server.registerTool('herald_'+name.replaceAll('.','_'),{
      description:descriptions[name as Command],inputSchema:schema,
      annotations:{readOnlyHint:reads.has(name),destructiveHint:admin.has(name),idempotentHint:reads.has(name)||['led.set','led.clear','audio.stop','settings.set'].includes(name),openWorldHint:['show.play','audio.play','audio.speak','demo.show','device.discover','device.ota'].includes(name)}
    },async(params:Record<string,unknown>)=>{
      try {
        const asynchronous=name.startsWith('ble.')||['show.play','led.pattern','led.reactive','audio.play','audio.speak','demo.show','device.ota','device.discover','wifi.scan','wifi.reconnect'].includes(name);
        const result=await client.invoke(name,params,asynchronous?0:10000);
        return output(result,result.result?.ok===false);
      } catch(e) {return output({error:(e as Error).message},true);}
    });
  }
  server.registerTool('herald_task_status',{description:'Show working (blue), success (green), or failure (red) with optional spoken text. Working continues until replaced; call success/failure/cancel when finished. Returns separate light and speech jobs.',inputSchema:z.object({phase:z.enum(['working','success','failure']),text:z.string().min(1).refine(s=>Buffer.byteLength(s)<=1000).optional()}),annotations:{readOnlyHint:false,destructiveHint:false,idempotentHint:false,openWorldHint:true}},async({phase,text})=>{
    try{const data=await client.notify(phase,text);return output(data,data.lights.result?.ok===false||data.speech?.result?.ok===false);}catch(e){return output({error:(e as Error).message},true);}
  });
  server.registerTool('herald_describe',{description:'Read live device capabilities and LAN administrative policy.',inputSchema:z.object({}),annotations:{readOnlyHint:true,idempotentHint:true,openWorldHint:false}},async()=>{
    try{return output(await client.describe());}catch(e){return output({error:(e as Error).message},true);}
  });
  server.registerTool('herald_job',{description:'Read a submitted job by its returned ID. Running is not success; inspect result.ok when done.',inputSchema:z.object({id:z.string().regex(/^[A-Za-z0-9_-]{1,64}$/)}),annotations:{readOnlyHint:true,idempotentHint:true,openWorldHint:false}},async({id})=>{
    try{const job=await client.status(id);return output(job,job.result?.ok===false);}catch(e){return output({error:(e as Error).message},true);}
  });
  server.registerTool('herald_cancel',{description:'Stop speaker playback and LED patterns owned by this LAN client, then release ownership. Does not cancel OTA or network scans.',inputSchema:z.object({}),annotations:{readOnlyHint:false,destructiveHint:false,idempotentHint:true,openWorldHint:false}},async()=>{
    try{return output(await client.cancel());}catch(e){return output({error:(e as Error).message},true);}
  });
  server.registerTool('herald_release',{description:'Release this client’s LAN ownership lease. Does not stop active effects; use herald_cancel for that.',inputSchema:z.object({}),annotations:{readOnlyHint:false,destructiveHint:false,idempotentHint:true,openWorldHint:false}},async()=>{
    try{return output(await client.release());}catch(e){return output({error:(e as Error).message},true);}
  });
  return server;
}
