#!/usr/bin/env node
import { HeraldClient, loadConfig } from './client.js';
import { provision } from './provision.js';
import { commands,type Command } from './schemas.js';
async function main() {
  const [command,arg,extra]=process.argv.slice(2);
  if(!command||command==='--help') {console.log('herald provision NEW_PRIVATE_DIRECTORY DEVICE_HOST\nherald describe | job ID | cancel | release\nherald notify working|success|failure [TEXT]\nherald COMMAND [JSON_PARAMS]\nSet HERALD_CONFIG and optional HERALD_OWNER. Commands return a job ID; query job until done.');return;}
  if(command==='provision') {if(!arg||!extra)throw new Error('Directory and device host required.');console.log(JSON.stringify(provision(arg,extra),null,2));return;}
  const client=new HeraldClient(loadConfig(),process.env.HERALD_OWNER??'cli');
  try {
    let result:unknown;
    if(command==='describe')result=await client.describe();
    else if(command==='job')result=await client.status(arg??'');
    else if(command==='cancel')result=await client.cancel();
    else if(command==='release')result=await client.release();
    else if(command==='notify') {if(!['working','success','failure'].includes(arg))throw new Error('notify expects working, success or failure');if(extra)commands['audio.speak'].parse({text:extra});result=await client.notify(arg as 'working'|'success'|'failure',extra);}
    else {const schema=commands[command as Command];if(!schema)throw new Error('Unknown command; run herald describe.');result=await client.submit(command,schema.parse(JSON.parse(arg??'{}')));}
    console.log(JSON.stringify(result,null,2));
    if((result as {result?:{ok:boolean}})?.result?.ok===false)process.exitCode=1;
  } finally {client.close();}
}
main().catch(e=>{console.error((e as Error).message);process.exitCode=1;});
