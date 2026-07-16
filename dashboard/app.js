"use strict";

const $ = id => document.getElementById(id);
const defaults = { view:"split", palette:"cividis", floor:-100, ceiling:-20, averaging:2, fps:10, overlay:true, history:600 };
function loadSettings() { try { return JSON.parse(localStorage.getItem("freedv-dashboard") || "{}"); } catch (_) { return {}; } }
const settings = Object.assign({}, defaults, loadSettings());
let statusData = null, historyData = [], socket = null, paused = false, lastRowAt = 0, reconnectDelay = 1000;
let averageBins = null;

const modeWidths = {"1600":1125,"700C":1500,"700D":1000,"700E":1500,"2400A":5000,"2400B":5000,"800XA":2000,"RADEV1":1500};
const colorStops = {
  cividis:[[0,[0,32,77]],[.25,[41,72,100]],[.5,[91,105,112]],[.75,[155,142,101]],[1,[253,231,55]]],
  viridis:[[0,[68,1,84]],[.25,[59,82,139]],[.5,[33,145,140]],[.75,[94,201,98]],[1,[253,231,37]]],
  grey:[[0,[10,13,16]],[1,[232,235,238]]]
};

function saveSettings() { localStorage.setItem("freedv-dashboard", JSON.stringify(settings)); applySettings(); }
function setText(id, value) { $(id).textContent = value == null || value === "" ? "—" : value; }
function number(value, digits=0) { return value == null || !Number.isFinite(+value) ? "—" : (+value).toFixed(digits); }
function frequency(value) { return value ? `${(+value/1e6).toFixed(6)} MHz` : "—"; }

function applySettings() {
  for (const key of ["view","palette","floor","ceiling","averaging","fps"]) $(key).value = settings[key];
  $("overlay").checked = !!settings.overlay;
  $("history-window").value = settings.history;
  document.querySelector(".signal-panel").dataset.view = settings.view;
  drawHistory();
}

async function api(path, options={}) {
  const response = await fetch(path, Object.assign({cache:"no-store"}, options));
  if (response.status === 401) { showLogin(); throw new Error("unauthorized"); }
  if (!response.ok) throw new Error(`request failed: ${response.status}`);
  return response.json();
}

function showLogin(message="") {
  $("dashboard").hidden = true; $("login").hidden = false; $("login-error").textContent = message;
  if (socket) { socket.close(); socket = null; }
}
function showDashboard() { $("login").hidden = true; $("dashboard").hidden = false; applySettings(); connectStream(); }

async function login(event) {
  event.preventDefault();
  try {
    const response = await fetch("/api/v1/login", {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify({token:$("access-key").value})});
    if (!response.ok) { showLogin(response.status === 429 ? "Too many attempts. Wait one minute." : "Invalid access key."); return; }
    $("access-key").value = ""; showDashboard(); await refresh();
  } catch (error) { showLogin("Dashboard is unavailable."); }
}

async function logout() { try { await fetch("/api/v1/logout", {method:"POST"}); } finally { showLogin(); } }

function badge(id, text, tone) { const el=$(id); el.textContent=text; el.className=`badge ${tone}`; }

function updateStatus(data) {
  statusData = data;
  const session = data.session || {active:false};
  badge("health-badge", data.kiwi_connected ? "Healthy" : "Kiwi disconnected", data.kiwi_connected ? "good" : "bad");
  badge("sync-badge", !session.active ? "Idle" : session.sync ? "Synchronized" : "No sync", !session.active ? "neutral" : session.sync ? "good" : "warn");
  setText("release", `v${data.release || "—"}`); setText("last-update", new Date().toLocaleTimeString());
  setText("mode", session.mode); setText("backend", session.backend); setText("frequency", frequency(session.frequency_hz));
  setText("input-rate", session.input_rate ? `${session.input_rate} Hz` : null);
  setText("snr", session.active ? `${number(session.snr_db,1)} dB` : null);
  setText("offset", session.active ? `${number(session.frequency_offset_hz,1)} Hz` : null);
  setText("reporter", data.reporter); setText("decoded-text", [session.callsign,session.text].filter(Boolean).join(" "));
  setText("axis", `0–${session.input_rate ? number(session.input_rate/2000,1) : "6"} kHz`);
  renderStats("modem-stats", session.modem || {}, [
    ["bits","Bits"],["bit_errors","Errors"],["packets","Packets"],["packet_errors","Packet errors"],
    ["resyncs","Resyncs"],["clock_offset_ppm","Clock offset (ppm)"],["timing_offset","Timing delta"],
    ["sync_metric","Sync metric"],["codec_variance","Codec variance"]]);
  renderStats("service-stats", data, [
    ["snd_frames_total","Input frames"],["decoded_frames_total","Decoded frames"],["dropped_frames_total","Dropped frames"],
    ["reconnects_total","Reconnects"],["auth_successes_total","Control auth successes"],["auth_failures_total","Control auth failures"],
    ["malformed_jobs_total","Malformed jobs"],["stale_jobs_total","Stale jobs"],["decode_seconds_total","Decode CPU seconds"],
    ["main_loop_age_seconds","Main-loop age (s)"]]);
  const extra = data.dashboard || {};
  for (const [key,label] of [["clients","Dashboard clients"],["waterfall_frames","Waterfall frames"],["spectrum_drops","Spectrum drops"],["audio_queue_ms","Audio queue (ms)"]]) addStat("service-stats", label, extra[key]);
}

function renderStats(id, source, fields) { const target=$(id); target.textContent=""; for(const [key,label] of fields) addStat(id,label,source[key]); }
function addStat(id,label,value) { const item=document.createElement("div"); item.className="stat"; const name=document.createElement("span"); name.textContent=label; const val=document.createElement("strong"); val.textContent=value == null ? "Not available" : typeof value === "number" && !Number.isInteger(value) ? value.toFixed(2) : value; item.append(name,val); $(id).append(item); }

async function refresh() {
  if ($("dashboard").hidden) return;
  try { const [status,history] = await Promise.all([api("/api/v1/status"),api("/api/v1/history")]); updateStatus(status); historyData=history; drawHistory(); }
  catch (error) { if (error.message !== "unauthorized") badge("health-badge","Dashboard unavailable","bad"); }
}

function connectStream() {
  if (socket || $("dashboard").hidden) return;
  const scheme=location.protocol === "https:" ? "wss" : "ws";
  socket=new WebSocket(`${scheme}://${location.host}/api/v1/stream`); socket.binaryType="arraybuffer";
  socket.onopen=()=>{reconnectDelay=1000;};
  socket.onmessage=event=>{if(socket?.readyState===WebSocket.OPEN)socket.send("ack");if(event.data instanceof ArrayBuffer&&!paused&&!document.hidden)consumeFrame(event.data);};
  socket.onclose=()=>{socket=null; if(!$("dashboard").hidden) setTimeout(connectStream,reconnectDelay); reconnectDelay=Math.min(reconnectDelay*2,10000);};
}

function consumeFrame(buffer) {
  const view=new DataView(buffer); if(view.byteLength<528||view.getUint32(0,false)!==0x46445746||view.getUint8(4)!==1)return;
  const bins=view.getUint16(6,true), rate=view.getUint32(8,true); if(bins!==512)return;
  const raw=new Float32Array(bins); for(let i=0;i<bins;i++) raw[i]=view.getUint8(16+i)*120/255-120;
  const n=+settings.averaging; if(!averageBins||averageBins.length!==bins)averageBins=raw.slice(); else for(let i=0;i<bins;i++)averageBins[i]+=(raw[i]-averageBins[i])/n;
  const now=Date.now(); if(now-lastRowAt<1000/+settings.fps)return; lastRowAt=now;
  drawWaterfall(averageBins); drawSpectrum(averageBins,rate);
}

function resizeCanvas(canvas) { const dpr=Math.min(devicePixelRatio||1,2), rect=canvas.getBoundingClientRect(), w=Math.max(1,Math.floor(rect.width*dpr)), h=Math.max(1,Math.floor(rect.height*dpr)); if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;} return {w,h,dpr}; }
function interpolate(stops,t) { for(let i=1;i<stops.length;i++){if(t<=stops[i][0]){const [p,a]=stops[i-1],[q,b]=stops[i],u=(t-p)/(q-p);return a.map((v,j)=>Math.round(v+(b[j]-v)*u));}}return stops.at(-1)[1]; }
function dbRange() { return Math.max(1, settings.ceiling-settings.floor); }
function color(db) { const t=Math.max(0,Math.min(1,(db-settings.floor)/dbRange())); return interpolate(colorStops[settings.palette],t); }

function drawWaterfall(bins) {
  const canvas=$("waterfall"),{w,h,dpr}=resizeCanvas(canvas),ctx=canvas.getContext("2d",{alpha:false});
  ctx.drawImage(canvas,0,0,w,h-1,0,1,w,h-1); const image=ctx.createImageData(w,1);
  for(let x=0;x<w;x++){const [r,g,b]=color(bins[Math.floor(x*bins.length/w)]),i=x*4;image.data[i]=r;image.data[i+1]=g;image.data[i+2]=b;image.data[i+3]=255;} ctx.putImageData(image,0,0); drawOverlay(ctx,w,h,dpr);
}
function drawSpectrum(bins,rate) {
  const canvas=$("spectrum"),{w,h,dpr}=resizeCanvas(canvas),ctx=canvas.getContext("2d",{alpha:false}); ctx.fillStyle="#0c0f13";ctx.fillRect(0,0,w,h);
  ctx.strokeStyle="#76a9d4";ctx.lineWidth=Math.max(1,dpr);ctx.beginPath(); for(let x=0;x<w;x++){const db=bins[Math.floor(x*bins.length/w)],y=h-(db-settings.floor)/dbRange()*h;x?ctx.lineTo(x,y):ctx.moveTo(x,y);}ctx.stroke(); drawOverlay(ctx,w,h,dpr,rate);
}
function drawOverlay(ctx,w,h,dpr,rate=(statusData?.session?.input_rate||12000)) { if(!settings.overlay)return; const nyquist=rate/2, mode=statusData?.session?.mode, width=modeWidths[mode]||0, low=(1500-width/2)/nyquist*w, high=(1500+width/2)/nyquist*w, center=1500/nyquist*w; ctx.save();ctx.strokeStyle="rgba(231,235,239,.32)";ctx.lineWidth=dpr;ctx.setLineDash([4*dpr,4*dpr]);ctx.strokeRect(low,0,Math.max(1,high-low),h);ctx.setLineDash([]);ctx.strokeStyle="rgba(208,167,95,.8)";ctx.beginPath();ctx.moveTo(center,0);ctx.lineTo(center,h);ctx.stroke();ctx.restore(); }

function drawHistory() { if(!historyData.length)return; const cutoff=Date.now()-settings.history*1000, points=historyData.filter(p=>p.timestamp_ms>=cutoff); drawLine($("snr-chart"),points.map(p=>p.snr_db),"#68b889"); drawLine($("offset-chart"),points.map(p=>p.frequency_offset_hz),"#76a9d4"); }
function drawLine(canvas,values,stroke) { const {w,h,dpr}=resizeCanvas(canvas),ctx=canvas.getContext("2d",{alpha:false});ctx.fillStyle="#151a21";ctx.fillRect(0,0,w,h);if(values.length<2)return;let min=Math.min(...values),max=Math.max(...values);if(min===max){min-=1;max+=1;}ctx.strokeStyle="#35404d";ctx.lineWidth=dpr;ctx.beginPath();ctx.moveTo(0,h/2);ctx.lineTo(w,h/2);ctx.stroke();ctx.strokeStyle=stroke;ctx.beginPath();values.forEach((v,i)=>{const x=i*w/(values.length-1),y=h-(v-min)/(max-min)*h;i?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke(); }
function clearPlots() { for(const id of ["waterfall","spectrum","snr-chart","offset-chart"]){const c=$(id),ctx=c.getContext("2d");ctx.fillStyle="#0c0f13";ctx.fillRect(0,0,c.width,c.height);} averageBins=null; }

$("login-form").addEventListener("submit",login); $("logout").addEventListener("click",logout);
for(const key of ["view","palette","floor","ceiling","averaging","fps"]){$(key).value=settings[key];$(key).addEventListener("change",e=>{settings[key]=key==="floor"||key==="ceiling"||key==="averaging"||key==="fps"?+e.target.value:e.target.value;saveSettings();});}
$("overlay").checked=settings.overlay;$("overlay").addEventListener("change",e=>{settings.overlay=e.target.checked;saveSettings();});
$("history-window").value=settings.history;$("history-window").addEventListener("change",e=>{settings.history=+e.target.value;saveSettings();});
$("pause").addEventListener("click",()=>{paused=!paused;$("pause").textContent=paused?"Resume":"Pause";}); $("clear").addEventListener("click",clearPlots);
document.addEventListener("visibilitychange",()=>{if(!document.hidden)drawHistory();}); window.addEventListener("resize",()=>{drawHistory();});

api("/api/v1/status").then(data=>{showDashboard();updateStatus(data);refresh();}).catch(()=>showLogin());
setInterval(refresh,1000);
