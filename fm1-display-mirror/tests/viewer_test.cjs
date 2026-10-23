const fs=require('fs'),vm=require('vm'),assert=require('assert');
const elements={};let draws=0,presents=0,now=0,raf=[];
const ctx={imageSmoothingEnabled:true,createImageData:()=>({data:new Uint8ClampedArray(240*240*4)}),putImageData:()=>draws++,drawImage:()=>presents++};
for(const name of ['canvas','#screen','#status','#placeholder','#scale','#playback','#fullscreen','main'])elements[name]={style:{},hidden:false,clientWidth:1000,clientHeight:800,value:'fit',addEventListener(){}};
elements['#playback'].value='smooth';elements.canvas.getContext=()=>ctx;
const sandbox={console,Uint8Array,ArrayBuffer,DataView,URL,Math,Number,JSON,Error,encodeURIComponent,
 document:{createElement:()=>({getContext:()=>ctx}),querySelector:n=>elements[n],addEventListener(){},fullscreenElement:null},
 window:{devicePixelRatio:2,addEventListener(){}},location:{href:'http://127.0.0.1:8787/?token=test',host:'127.0.0.1:8787'},
 WebSocket:class{close(){}},performance:{now:()=>now},requestAnimationFrame:cb=>raf.push(cb),setTimeout(){},innerWidth:1000,innerHeight:800};
vm.createContext(sandbox);vm.runInContext(fs.readFileSync('fm1-display-mirror/web/viewer.js','utf8'),sandbox);
function tick(t){now=t;const cb=raf.shift();assert(cb);cb(t);assert.equal(raf.length,1);}
function record(kind,body=Buffer.alloc(0)){
 const h=Buffer.alloc(28);h.write('FMIR');h[4]=1;h[5]=kind;h[6]=kind===2?1:0;h.writeUInt16LE(240,12);h.writeUInt16LE(240,14);
 if(kind===3){h.writeUInt16LE(240,20);h.writeUInt16LE(240,22);}h.writeUInt32LE(body.length,24);return Buffer.concat([h,body]);
}
function fixture(value){const wire=Buffer.concat([record(2),record(3,Buffer.alloc(115200,value)),record(4)]);return wire.buffer.slice(wire.byteOffset,wire.byteOffset+wire.length);}
sandbox.fixture=fixture(255);
vm.runInContext('decode(fixture);decode(fixture)',sandbox);assert.equal(draws,0);
tick(0);tick(16.67);assert.equal(draws,0);tick(33.34);assert.equal(draws,1);assert.equal(elements.canvas.style.visibility,'visible');
vm.runInContext('hide("Disconnected");decode(fixture)',sandbox);tick(66.68);assert.equal(draws,2);
sandbox.bad=new ArrayBuffer(28);assert.throws(()=>vm.runInContext('decode(bad)',sandbox));
assert.equal(ctx.imageSmoothingEnabled,false);assert.equal(elements.canvas.style.width,'720px');assert.equal(elements.canvas.width,1440);
vm.runInContext('resize()',sandbox);assert.equal(ctx.imageSmoothingEnabled,false);
// Bursts replace older complete snapshots; bounded queue and one paint per cadence slot.
for(let i=0;i<10;i++){sandbox.next=fixture(i);vm.runInContext('decode(next)',sandbox);}
assert.equal(vm.runInContext('pending.length',sandbox),3);
const before=presents;tick(83.35);assert.equal(presents,before);tick(100.02);assert.equal(presents,before+1);
assert.equal(vm.runInContext('previous[0]',sandbox),9);assert.equal(vm.runInContext('pending.length',sandbox),0);
// 60 fps uses a one-refresh buffer and can show a new frame every 16.67 ms.
elements['#playback'].value='smooth60';vm.runInContext('nextPaint=0',sandbox);
sandbox.next=fixture(20);vm.runInContext('decode(next)',sandbox);tick(116.69);
assert.equal(vm.runInContext('previous[0]',sandbox),20);
sandbox.next=fixture(21);vm.runInContext('decode(next)',sandbox);tick(133.36);
assert.equal(vm.runInContext('previous[0]',sandbox),21);
// Lowest-delay mode uses the next display refresh, without the jitter buffer.
elements['#playback'].value='live';sandbox.next=fixture(42);vm.runInContext('decode(next)',sandbox);tick(150.03);
assert.equal(vm.runInContext('previous[0]',sandbox),42);
// Disconnect removes queued old screens, including if the tab resumes later.
vm.runInContext('decode(fixture);hide("Disconnected")',sandbox);tick(1000);assert.equal(vm.runInContext('pending.length',sandbox),0);assert.equal(elements.canvas.style.visibility,'hidden');
console.log('viewer: exact pixels, bounded jitter buffer, 30/60-fps cadence, latest-frame selection, lowest-delay mode, disconnect clearing and Retina scaling passed');
