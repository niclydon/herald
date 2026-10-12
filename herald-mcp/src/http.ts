import { createServer as httpsServer } from 'node:https';
import { readFileSync,statSync } from 'node:fs';
import { timingSafeEqual } from 'node:crypto';
import { NodeStreamableHTTPServerTransport,hostHeaderValidation,originValidation } from '@modelcontextprotocol/node';
import { HeraldClient,loadConfig } from './client.js';
import { createServer } from './tools.js';
interface HttpConfig { host:string;port:number;certFile:string;keyFile:string;allowedHosts:string[];clients:Record<string,{token:string;admin?:boolean}> }
export async function serveHttp() {
  const file=process.env.HERALD_HTTP_CONFIG;if(!file) throw new Error('Set HERALD_HTTP_CONFIG to a private HTTPS bridge configuration.');
  if(process.platform!=='win32'&&(statSync(file).mode&0o077))throw new Error('Bridge config requires chmod 600.');
  const cfg=JSON.parse(readFileSync(file,'utf8')) as HttpConfig;
  if(!cfg.allowedHosts?.length||!Object.keys(cfg.clients??{}).length)throw new Error('Configure allowedHosts and at least one named client.');
  for(const [name,c] of Object.entries(cfg.clients)) if(!/^[A-Za-z0-9_-]{1,64}$/.test(name)||c.token?.length<32)throw new Error('Each client needs a valid owner name and a random token of at least 32 characters.');
  const device=loadConfig();const host=hostHeaderValidation(cfg.allowedHosts),origin=originValidation(cfg.allowedHosts);
  const http=httpsServer({cert:readFileSync(cfg.certFile),key:readFileSync(cfg.keyFile)},async(req,res)=>{
    if(!host(req,res)||!origin(req,res))return;
    if(req.url!=='/mcp'||req.method!=='POST'){res.writeHead(404).end();return;}
    const got=Buffer.from(req.headers.authorization??'');
    const match=Object.entries(cfg.clients).find(([,c])=>{const expected=Buffer.from(`Bearer ${c.token}`);return expected.length===got.length&&timingSafeEqual(expected,got);});
    if(!match){res.writeHead(401,{'WWW-Authenticate':'Bearer'}).end();return;}
    if(Number(req.headers['content-length']??0)>65536){res.writeHead(413).end();return;}
    const chunks:Buffer[]=[];let bytes=0;
    try {
      for await(const chunk of req){const b=Buffer.from(chunk);bytes+=b.length;if(bytes>65536){res.writeHead(413).end();return;}chunks.push(b);}
    } catch {if(!res.headersSent&&!res.destroyed)res.writeHead(400).end();return;}
    let body:unknown;try{body=JSON.parse(Buffer.concat(chunks).toString());}catch{res.writeHead(400).end();return;}
    const client=new HeraldClient({...device,admin:!!device.admin&&!!match[1].admin},match[0]);
    const server=createServer(client);
    const transport=new NodeStreamableHTTPServerTransport({sessionIdGenerator:undefined,enableJsonResponse:true});
    try {await server.connect(transport);await transport.handleRequest(req,res,body);}
    catch {if(!res.headersSent)res.writeHead(500).end();}
    finally {client.close();await transport.close();await server.close();}
  });
  http.requestTimeout=30000;http.headersTimeout=10000;
  await new Promise<void>(resolve=>http.listen(cfg.port??9443,cfg.host??'127.0.0.1',resolve));
  console.error('Herald MCP HTTPS bridge listening');
}
