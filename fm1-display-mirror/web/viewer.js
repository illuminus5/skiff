'use strict';
// Receives pixels only. No Web MIDI, USB, UI emulation, or hardware commands.
const canvas=document.querySelector('canvas'),ctx=canvas.getContext('2d',{alpha:false});
const screen=document.querySelector('#screen'),status=document.querySelector('#status');
const placeholder=document.querySelector('#placeholder'),scale=document.querySelector('#scale');
const playback=document.querySelector('#playback'),presentation=document.querySelector('#presentation');
const source=document.createElement('canvas');source.width=source.height=240;
const sourceCtx=source.getContext('2d',{alpha:false});
ctx.imageSmoothingEnabled=false;
const image=ctx.createImageData(240,240),previous=new Uint8Array(115200);
let haveImage=false, socket, generation=0;
let pending=[],nextPaint=0,shown=[],streamStatus=null,lastReceived=-Infinity;
function targetRate(){return playback.value==='smooth60'?60:playback.value==='smooth'?30:0;}
function updateStatus(now){
 while(shown.length&&now-shown[0]>2000)shown.shift();
 if(!streamStatus)return;
 const s=streamStatus;
 presentation.hidden=!s.diagnostics;
 if(!s.diagnostics){
  status.textContent=s.state==='connected'?'Connected · Skiff':s.state==='synchronizing'?'Connecting to Skiff…':`Disconnected · ${s.detail}`;
  return;
 }
 const fps=shown.length>1?(shown.length-1)*1000/(shown[shown.length-1]-shown[0]):0;
 status.textContent=`${s.state} · ${s.firmware||'No firmware'} · source ${now-lastReceived>2000?0:Number(s.fps)||0} fps · shown ${fps.toFixed(1)} fps · max gap ${Number(s.gap_ms)||0} ms · ${targetRate()?`${targetRate()} Hz / ${Math.round(1000/targetRate())} ms buffer`:'lowest delay'} · ${s.detail}`;
}
function animate(now){
 const rate=targetRate(),smooth=rate>0;
 if(!smooth||now+.2>=nextPaint){
  if(smooth){nextPaint+=1000/rate;if(nextPaint<now)nextPaint=now+1000/rate;}
  let pixels=null;
  while(pending.length&&pending[0].due<=now)pixels=pending.shift().pixels;
  if(pixels){renderPixels(pixels);shown.push(now);if(shown.length>120)shown.shift();}
 }
 updateStatus(now);requestAnimationFrame(animate);
}
function hide(message){pending=[];shown=[];nextPaint=0;canvas.style.visibility='hidden';placeholder.hidden=false;placeholder.textContent=message;haveImage=false;}
function present(){
 ctx.imageSmoothingEnabled=false;
 ctx.drawImage(source,0,0,canvas.width,canvas.height);
}
function resize(){
 const fullscreen=!!document.fullscreenElement;
 const main=document.querySelector('main');
 // Physical pixels are integer multiples of source pixels even on Retina.
 const dpr=window.devicePixelRatio||1;
 const width=fullscreen?innerWidth:main.clientWidth-32,height=fullscreen?innerHeight:main.clientHeight-32;
 const multiple=scale.value==='fit'||fullscreen?Math.max(1,Math.floor(Math.min(width,height)*dpr/240)):Number(scale.value)*dpr;
 const size=Math.floor(multiple)*240/dpr;
 canvas.width=canvas.height=Math.round(size*dpr);
 canvas.style.width=size+'px';canvas.style.height=size+'px';
 if(haveImage)present();
 screen.style.width=fullscreen?'100%':size+'px';screen.style.height=fullscreen?'100%':size+'px';
}
function parse(buffer){
 if(!(buffer instanceof ArrayBuffer)||buffer.byteLength>116000)throw Error('Invalid frame size');
 const d=new DataView(buffer);let p=0,seq=null,pixels=null,committed=false;
 while(p<d.byteLength){
  if(p+28>d.byteLength||d.getUint32(p,false)!==0x464d4952||d.getUint8(p+4)!==1)throw Error('Invalid protocol');
  const kind=d.getUint8(p+5),flags=d.getUint8(p+6),codec=d.getUint8(p+7),s=d.getUint32(p+8,true);
  const width=d.getUint16(p+12,true),height=d.getUint16(p+14,true),x=d.getUint16(p+16,true),y=d.getUint16(p+18,true),w=d.getUint16(p+20,true),h=d.getUint16(p+22,true),n=d.getUint32(p+24,true);
  p+=28;if(width!==240||height!==240||n>115200||p+n>d.byteLength)throw Error('Invalid dimensions');
  if(kind===2){if(seq!==null||flags!==1||n)throw Error('Invalid begin');seq=s;}
  else if(kind===3){if(seq!==s||pixels||codec!==0||x||y||w!==240||h!==240||n!==115200)throw Error('Invalid snapshot');pixels=new Uint8Array(buffer,p,n);}
  else if(kind===4){if(seq!==s||!pixels||n||p!==d.byteLength)throw Error('Invalid commit');committed=true;}
  else throw Error('Unknown message');
  p+=n;
 }
 if(!committed)throw Error('Incomplete transaction');
 return pixels;
}
function decode(buffer){
 const pixels=parse(buffer),now=performance.now();lastReceived=now;
 pending.push({pixels,due:now+(targetRate()?1000/targetRate():0)});
 if(pending.length>3)pending.shift();
}
function renderPixels(pixels){
 let minX=240,minY=240,maxX=-1,maxY=-1;
 for(let i=0;i<57600;i++){
  const j=i*2;if(haveImage&&pixels[j]===previous[j]&&pixels[j+1]===previous[j+1])continue;
  const c=(pixels[j]<<8)|pixels[j+1],r=(c>>11)&31,g=(c>>5)&63,b=c&31,k=i*4;
  image.data[k]=(r<<3)|(r>>2);image.data[k+1]=(g<<2)|(g>>4);image.data[k+2]=(b<<3)|(b>>2);image.data[k+3]=255;
  const x=i%240,y=Math.floor(i/240);minX=Math.min(minX,x);maxX=Math.max(maxX,x);minY=Math.min(minY,y);maxY=Math.max(maxY,y);
 }
 if(maxX>=0){sourceCtx.putImageData(image,0,0,minX,minY,maxX-minX+1,maxY-minY+1);present();}
 previous.set(pixels);haveImage=true;canvas.style.visibility='visible';placeholder.hidden=true;
}
function connect(){
 const mine=++generation,token=new URL(location.href).searchParams.get('token');
 if(!token){hide('Open the private URL printed by the bridge');return;}
 socket=new WebSocket(`ws://${location.host}/ws?token=${encodeURIComponent(token)}`);socket.binaryType='arraybuffer';
 socket.onmessage=e=>{if(mine!==generation)return;try{
  if(typeof e.data==='string'){
   if(e.data.length>2048)throw Error('Invalid status');const s=JSON.parse(e.data);
   if(!['connected','disconnected','synchronizing'].includes(s.state)||typeof s.detail!=='string')throw Error('Invalid status');
   streamStatus=s;updateStatus(performance.now());
   if(s.state!=='connected')hide(s.state==='synchronizing'?'Synchronizing display…':'Device disconnected');
  }else decode(e.data);
 }catch(error){streamStatus=null;hide('Invalid display data');status.textContent=error.message;socket.close();}};
 socket.onclose=()=>{if(mine!==generation)return;streamStatus=null;hide('Bridge disconnected');status.textContent='Disconnected · reconnecting';setTimeout(connect,1500);};
 socket.onerror=()=>socket.close();
}
playback.addEventListener('change',()=>{
 const last=pending[pending.length-1];pending=last?[{pixels:last.pixels,due:performance.now()}]:[];
 nextPaint=0;shown=[];
});
scale.addEventListener('change',resize);window.addEventListener('resize',resize);document.addEventListener('fullscreenchange',resize);
document.querySelector('#fullscreen').addEventListener('click',async()=>{try{if(document.fullscreenElement)await document.exitFullscreen();else await screen.requestFullscreen();}catch(error){status.textContent='Fullscreen unavailable: '+error.message;}});
resize();connect();requestAnimationFrame(animate);
