const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const elements=new Map();
function element(){return {textContent:'',className:'',dataset:{},children:[],append(...nodes){this.children.push(...nodes);},replaceChildren(){this.children=[];}};}
const document={hidden:false,getElementById(id){if(!elements.has(id))elements.set(id,element());return elements.get(id);},querySelector(){return element();},createElement:element};
const context=vm.createContext({document,localStorage:{getItem:()=>null},Date,console});
const source=fs.readFileSync('dashboard/app.js','utf8');
vm.runInContext(source.split('for(const key of ["view","palette","floor"')[0],context);
const now=Date.now()/1000;
const activity={state:'online',transmitters:[{callsign:'VK5DGR',mode:'RADEV1',frequency_hz:14236000}],receptions:[
 {receiver_callsign:'ZL2ABC',heard_callsign:'VK5DGR',mode:'RADEV1',frequency_hz:14236000,snr_db:20,timestamp:now-5},
 {receiver_callsign:'G4AAA',heard_callsign:null,mode:'RADEV2',frequency_hz:null,snr_db:null,timestamp:now-5},
 {receiver_callsign:'G4BBB',heard_callsign:'VK5DGR',mode:'RADEV1',frequency_hz:14236000,snr_db:8,timestamp:now-700}]};
const session={active:true,sync:true,frequency_hz:14236000,mode:'RADEV1',snr_db:5,frequency_offset_hz:1};
assert.equal(context.reporterRows(activity,session,'current',now).receptions.length,1);
assert.equal(context.reporterRows(activity,session,'all',now).receptions.length,2);
assert.equal(context.reporterRows({...activity,state:'stale'},session,'all',now).receptions.length,0);
context.updateStatus({kiwi_connected:true,session,reporter:'disabled',reporter_activity:activity});
assert.equal(elements.get('snr').textContent,'5.0 dB');
assert.equal(elements.get('reporter-receptions').children[0].children[3].textContent,'20.0 dB');
context.updateStatus({kiwi_connected:true,session:{...session,sync:false},reporter_activity:activity});
assert.equal(elements.get('snr').textContent,'—');
assert.equal(context.reporterRows(activity,{active:false},'current',now).receptions.length,2);
assert.deepEqual(Array.from(context.historyValues([{sync:true,snr_db:0},{sync:false,snr_db:0},{sync:true,snr_db:5}],'snr_db')),[0,null,5]);
console.log('dashboard source separation, freshness and frequency filtering passed');
