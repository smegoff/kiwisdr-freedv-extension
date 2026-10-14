const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const context = vm.createContext({w3_el:()=>null});
vm.runInContext(fs.readFileSync(path.join(__dirname,
  '../kiwi-overlay/web/extensions/FreeDV/FreeDV.js'), 'utf8'), context);
context.freedv_update_reporter_frequencies('14236000:ZL2ABC:RADEV2;7177000;14236000');
assert.equal(context.freedv.reporter_frequencies[0].transmitting, true);
assert.match(context.freedv.reporter_frequencies[0].label, /ZL2ABC.*RADEV2/);
assert.equal(context.freedv.reporter_frequencies[0].mode, 'RADEV2');
context.freedv.calling_index = 1;
context.freedv_update_reporter_frequencies('14236000:ZL2ABC:RADEV2;3625000;7177000');
assert.equal(context.freedv.calling_index, 1);
context.freedv_update_reporter_frequencies('14236000:<script>:RADEV2;bad;14236000');
assert.equal(context.freedv.reporter_frequencies.filter(s=>s.transmitting).length, 0);
context.freedv_update_reporter_frequencies('-');
assert.equal(context.freedv.reporter_frequencies.length, 0);
assert.ok(context.freedv_calling_entries().length >= 18);
for (const [mode, label] of [['RADEV2','Test RADEv2'],['RADEV1','Test RADE'],['700D','Test 700D']]) {
  context.freedv.mode = mode;
  assert.equal(context.freedv_test_button_label(), label);
}
console.log('extension station feed behavior passed');
const bins = '00'.repeat(256);
assert.equal(context.freedv_proxy_frame({version:1, sequence:1, sample_rate:12000,bins}).length,256);
for (const bad of [null, {}, {version:2,sequence:1,sample_rate:12000,bins},
  {version:1,sequence:1,sample_rate:1,bins},
  {version:1,sequence:1,sample_rate:12000,bins:'ff'.repeat(256)},
  {version:1,sequence:1,sample_rate:12000,bins:'<script>'}]) {
  assert.equal(context.freedv_proxy_frame(bad),null);
}
console.log('proxy diagnostics malformed-frame behavior passed');
let paints=0;
const canvasContext={clearRect(){},drawImage(){paints++;},fillRect(){},beginPath(){},
  moveTo(){},lineTo(){},stroke(){},fillText(){}};
const section={open:false},readout={textContent:''};
context.document={hidden:false};
context.w3_el=id=>({'id-freedv-proxy-diagnostics':section,
  'id-freedv-proxy-canvas':{getContext:()=>canvasContext},
  'id-freedv-proxy-readout':readout}[id]);
const signal={generation:1,sync:true,snr:12.5,frequency_offset:2,
  diagnostics:{version:1,sequence:1,sample_rate:12000,bins}};
context.freedv_proxy_diagnostics(signal); assert.equal(paints,0);
section.open=true;
context.freedv_proxy_diagnostics(signal); assert.equal(paints,1);
assert.match(readout.textContent,/Local SNR: 12.5 dB/);
context.freedv_proxy_diagnostics(signal); assert.equal(paints,1);
context.document.hidden=true;
context.freedv_proxy_diagnostics({...signal,diagnostics:{...signal.diagnostics,sequence:2}});
assert.equal(paints,1);
context.document.hidden=false;
context.freedv_proxy_diagnostics({...signal,generation:2}); assert.equal(paints,2);
console.log('proxy rendering collapse, suspension, deduplication and generation tests passed');
