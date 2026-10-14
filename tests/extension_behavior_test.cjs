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
