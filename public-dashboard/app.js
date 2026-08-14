"use strict";

const $ = id => document.getElementById(id);
const defaults = {palette:"cividis",autoLevels:true,floor:-100,ceiling:-20,averaging:2,overlay:true};
function loadSettings(){try{return JSON.parse(localStorage.getItem("freedv-public-dashboard")||"{}");}catch(_){return {};}}
const settings=Object.assign({},defaults,loadSettings());
let statusData=null,historyData=[],socket=null,paused=false,averageBins=null,lastRowAt=0,reconnectDelay=1000;
let autoFloor=null,autoCeiling=null,lastRangeAt=0,context="";

const modeWidths={"1600":1125,"700C":1500,"700D":1000,"700E":1500,"2400A":5000,"2400B":5000,"800XA":2000,"RADEV1":1500};
const stops={
  cividis:[[0,[0,32,77]],[.25,[41,72,100]],[.5,[91,105,112]],[.75,[155,142,101]],[1,[253,231,55]]],
  viridis:[[0,[68,1,84]],[.25,[59,82,139]],[.5,[33,145,140]],[.75,[94,201,98]],[1,[253,231,37]]],
  grey:[[0,[10,13,16]],[1,[232,235,238]]],
  turbo:[[0,[48,18,59]],[.2,[49,91,222]],[.4,[24,188,177]],[.6,[164,252,60]],[.8,[250,151,31]],[1,[122,4,2]]]
};
function interpolate(points,t){for(let i=1;i<points.length;i++){if(t<=points[i][0]){const [p,a]=points[i-1],[q,b]=points[i],u=(t-p)/(q-p);return a.map((v,j)=>Math.round(v+(b[j]-v)*u));}}return points.at(-1)[1];}
const palettes=Object.fromEntries(Object.entries(stops).map(([name,value])=>[name,Array.from({length:256},(_,i)=>interpolate(value,i/255))]));
function save(){localStorage.setItem("freedv-public-dashboard",JSON.stringify(settings));applySettings();}
function setText(id,value){$(id).textContent=value==null||value===""?"—":value;}
function number(value,digits=0){return value==null||!Number.isFinite(+value)?"—":(+value).toFixed(digits);}
function badge(id,text,tone){const el=$(id);el.textContent=text;el.className=`badge ${tone}`;}
function clamp(value,low,high){return Math.max(low,Math.min(high,value));}
function percentile(sorted,fraction){return sorted[Math.min(sorted.length-1,Math.max(0,Math.round((sorted.length-1)*fraction)))];}
function effectiveFloor(){return settings.autoLevels&&autoFloor!=null?autoFloor:+settings.floor;}
function effectiveCeiling(){return settings.autoLevels&&autoCeiling!=null?autoCeiling:+settings.ceiling;}
function dbRange(){return Math.max(1,effectiveCeiling()-effectiveFloor());}

function applySettings(){
  $("palette").value=settings.palette;$("auto-levels").checked=!!settings.autoLevels;
  $("floor").disabled=!!settings.autoLevels;$("ceiling").disabled=!!settings.autoLevels;
  $("averaging").value=settings.averaging;$("overlay").checked=!!settings.overlay;updateRange(true);
}
function resetLevels(){autoFloor=null;autoCeiling=null;lastRangeAt=0;updateRange(true);}
function updateLevels(bins){
  const sorted=Array.from(bins).filter(Number.isFinite).sort((a,b)=>a-b);if(sorted.length<16)return;
  const noise=percentile(sorted,.20),peak=percentile(sorted,.995);
  let wantedFloor=clamp(noise-6,-120,-35),wantedCeiling=clamp(Math.max(peak+3,wantedFloor+30),-90,0);
  if(wantedCeiling-wantedFloor<30)wantedFloor=clamp(wantedCeiling-30,-120,-35);
  if(wantedCeiling-wantedFloor>70)wantedFloor=wantedCeiling-70;
  if(autoFloor==null||autoCeiling==null){autoFloor=wantedFloor;autoCeiling=wantedCeiling;}
  else {const fa=wantedFloor<autoFloor ? .30 : .06,ca=wantedCeiling>autoCeiling ? .30 : .06;autoFloor+=(wantedFloor-autoFloor)*fa;autoCeiling+=(wantedCeiling-autoCeiling)*ca;}
  if(autoCeiling-autoFloor<30)autoCeiling=Math.min(0,autoFloor+30);if(autoCeiling-autoFloor>70)autoFloor=autoCeiling-70;updateRange();
}
function updateRange(force=false){
  const now=Date.now();if(!force&&now-lastRangeAt<250)return;lastRangeAt=now;
  if(settings.autoLevels&&autoFloor!=null){$("floor").value=autoFloor.toFixed(1);$("ceiling").value=autoCeiling.toFixed(1);$("range").textContent=`Auto ${autoFloor.toFixed(1)} to ${autoCeiling.toFixed(1)} dBFS`;}
  else if(settings.autoLevels){$("range").textContent="Auto range waiting for signal";}
  else {$("floor").value=settings.floor;$("ceiling").value=settings.ceiling;$("range").textContent=`Manual ${settings.floor} to ${settings.ceiling} dBFS`;}
}
async function api(path){const response=await fetch(path,{cache:"no-store"});if(!response.ok)throw new Error(response.status);return response.json();}
function updateStatus(data){
  statusData=data;const session=data.session||{active:false},nextContext=`${!!session.active}:${session.mode||""}:${session.input_rate||0}`;
  if(context&&context!==nextContext)resetLevels();context=nextContext;
  badge("health",data.kiwi_connected?"Receiver online":"Receiver unavailable",data.kiwi_connected?"good":"bad");
  badge("sync",!session.active?"Idle":session.sync?"Synchronized":"Receiving",!session.active?"neutral":session.sync?"good":"warn");
  setText("release",`v${data.release||"—"}`);setText("updated",new Date().toLocaleTimeString());
  setText("mode",session.mode);setText("frequency",session.frequency_hz?`${(session.frequency_hz/1e6).toFixed(6)} MHz`:null);
  setText("input-rate",session.input_rate?`${session.input_rate} Hz`:null);setText("snr",session.active?`${number(session.snr_db,1)} dB`:null);
  setText("offset",session.active?`${number(session.frequency_offset_hz,1)} Hz`:null);setText("source",session.active?(session.test?"Reference test":"Live receiver"):null);
  setText("axis",`0–${session.input_rate?number(session.input_rate/2000,1):"6"} kHz`);
}
async function refreshStatus(){try{updateStatus(await api("/api/v1/status"));}catch(_){badge("health","Monitor unavailable","bad");}}
async function refreshHistory(){try{historyData=await api("/api/v1/history");drawHistory();}catch(_) {}}
function connect(){if(socket)return;const scheme=location.protocol==="https:"?"wss":"ws";socket=new WebSocket(`${scheme}://${location.host}/api/v1/stream`);socket.binaryType="arraybuffer";socket.onopen=()=>{reconnectDelay=1000;};socket.onmessage=e=>{if(socket?.readyState===WebSocket.OPEN)socket.send("ack");if(e.data instanceof ArrayBuffer&&!paused&&!document.hidden)consume(e.data);};socket.onclose=()=>{socket=null;setTimeout(connect,reconnectDelay);reconnectDelay=Math.min(reconnectDelay*2,10000);};}
function consume(buffer){const view=new DataView(buffer);if(view.byteLength<528||view.getUint32(0,false)!==0x46445746||view.getUint8(4)!==1)return;const count=view.getUint16(6,true),rate=view.getUint32(8,true);if(count!==512)return;const raw=new Float32Array(count);for(let i=0;i<count;i++)raw[i]=view.getUint8(16+i)*120/255-120;const n=+settings.averaging;if(!averageBins)averageBins=raw.slice();else for(let i=0;i<count;i++)averageBins[i]+=(raw[i]-averageBins[i])/n;if(settings.autoLevels)updateLevels(averageBins);if(Date.now()-lastRowAt<100)return;lastRowAt=Date.now();drawWaterfall(averageBins);drawSpectrum(averageBins,rate);}
function resize(canvas){const dpr=Math.min(devicePixelRatio||1,2),rect=canvas.getBoundingClientRect(),w=Math.max(1,Math.floor(rect.width*dpr)),h=Math.max(1,Math.floor(rect.height*dpr));if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;}return{w,h,dpr};}
function color(db){const t=clamp((db-effectiveFloor())/dbRange(),0,1),palette=palettes[settings.palette]||palettes.cividis;return palette[Math.round(t*255)];}
function drawWaterfall(bins){const canvas=$("waterfall"),{w,h,dpr}=resize(canvas),ctx=canvas.getContext("2d",{alpha:false});ctx.drawImage(canvas,0,0,w,h-1,0,1,w,h-1);const image=ctx.createImageData(w,1);for(let x=0;x<w;x++){const [r,g,b]=color(bins[Math.floor(x*bins.length/w)]),i=x*4;image.data[i]=r;image.data[i+1]=g;image.data[i+2]=b;image.data[i+3]=255;}ctx.putImageData(image,0,0);overlay(ctx,w,h,dpr);}
function drawSpectrum(bins,rate){const canvas=$("spectrum"),{w,h,dpr}=resize(canvas),ctx=canvas.getContext("2d",{alpha:false});ctx.fillStyle="#0c0f13";ctx.fillRect(0,0,w,h);ctx.strokeStyle="#76a9d4";ctx.lineWidth=Math.max(1,dpr);ctx.beginPath();for(let x=0;x<w;x++){const db=bins[Math.floor(x*bins.length/w)],y=h-(db-effectiveFloor())/dbRange()*h;x?ctx.lineTo(x,y):ctx.moveTo(x,y);}ctx.stroke();overlay(ctx,w,h,dpr,rate);}
function overlay(ctx,w,h,dpr,rate=(statusData?.session?.input_rate||12000)){if(!settings.overlay)return;const nyquist=rate/2,width=modeWidths[statusData?.session?.mode]||0,low=(1500-width/2)/nyquist*w,high=(1500+width/2)/nyquist*w,center=1500/nyquist*w;ctx.save();ctx.strokeStyle="rgba(231,235,239,.32)";ctx.lineWidth=dpr;ctx.setLineDash([4*dpr,4*dpr]);ctx.strokeRect(low,0,Math.max(1,high-low),h);ctx.setLineDash([]);ctx.strokeStyle="rgba(208,167,95,.8)";ctx.beginPath();ctx.moveTo(center,0);ctx.lineTo(center,h);ctx.stroke();ctx.restore();}
function drawHistory(){drawLine($("snr-history"),historyData.map(p=>p.snr_db),"#68b889");drawLine($("offset-history"),historyData.map(p=>p.frequency_offset_hz),"#76a9d4");}
function drawLine(canvas,values,stroke){const {w,h,dpr}=resize(canvas),ctx=canvas.getContext("2d",{alpha:false});ctx.fillStyle="#151a21";ctx.fillRect(0,0,w,h);if(values.length<2)return;let min=Math.min(...values),max=Math.max(...values);if(min===max){min-=1;max+=1;}ctx.strokeStyle=stroke;ctx.lineWidth=dpr;ctx.beginPath();values.forEach((v,i)=>{const x=i*w/(values.length-1),y=h-(v-min)/(max-min)*h;i?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();}
function clear(){for(const id of ["waterfall","spectrum","snr-history","offset-history"]){const canvas=$(id),ctx=canvas.getContext("2d");ctx.fillStyle="#0c0f13";ctx.fillRect(0,0,canvas.width,canvas.height);}averageBins=null;resetLevels();}
for(const key of ["palette","floor","ceiling","averaging"]){$(key).value=settings[key];$(key).addEventListener("change",e=>{settings[key]=key==="palette"?e.target.value:+e.target.value;save();});}
$("auto-levels").checked=!!settings.autoLevels;$("auto-levels").addEventListener("change",e=>{settings.autoLevels=e.target.checked;resetLevels();save();});$("overlay").checked=!!settings.overlay;$("overlay").addEventListener("change",e=>{settings.overlay=e.target.checked;save();});$("pause").addEventListener("click",()=>{paused=!paused;$("pause").textContent=paused?"Resume":"Pause";});$("clear").addEventListener("click",clear);document.addEventListener("visibilitychange",()=>{if(!document.hidden)drawHistory();});window.addEventListener("resize",drawHistory);
applySettings();connect();refreshStatus();refreshHistory();setInterval(refreshStatus,2000);setInterval(refreshHistory,15000);
