#!/usr/bin/env node
import { StdioServerTransport } from '@modelcontextprotocol/server/stdio';
import { HeraldClient, loadConfig } from './client.js';
import { createServer } from './tools.js';
async function main() {
  if(process.argv.includes('--http')) { const { serveHttp }=await import('./http.js'); await serveHttp(); return; }
  const client=new HeraldClient(loadConfig(),process.env.HERALD_OWNER);
  const server=createServer(client);
  process.once('SIGTERM',()=>{client.close();void server.close();});
  await server.connect(new StdioServerTransport());
}
main().catch(e=>{console.error((e as Error).message);process.exitCode=1;});
