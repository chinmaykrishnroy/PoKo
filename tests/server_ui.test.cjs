// Run the production dashboard script against deterministic HTTP/DOM doubles.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');

function dashboard(){
  const nodes=new Map(),calls=[],writes=[];
  function element(id){if(!nodes.has(id)){let html='';nodes.set(id,{get innerHTML(){return html},set innerHTML(value){html=value;if(id==='player')writes.push(value)},style:{},classList:{add(){},remove(){}},textContent:'',onclick:null,onsubmit:null});}return nodes.get(id);}
  const state={operation:{status:'running'},playback:{active:true,item_id:'chosen',title:'Chosen song',artist:'Chosen artist',kind:'audio',position_s:1,mode:'audio_player'},filters:{filters:{highpass:{enabled:false,cutoff_hz:80},lowpass:{enabled:false,cutoff_hz:16000}}}};
  const context=vm.createContext({document:{getElementById:element,querySelectorAll:()=>[]},setTimeout:()=>0,clearTimeout(){},setInterval(){},location:{replace(){}},console,
    fetch:async url=>{calls.push(url);let data;if(url.includes('/play?'))data={ok:true,accepted:true,operation_id:1};else if(url.includes('/operation?'))data=state.operation;else if(url==='/api/playback/status')data=state.statusGate?await state.statusGate:state.playback;else if(url==='/api/server/audio-filters')data=state.filters;else throw Error(url);return {ok:true,status:200,text:async()=>JSON.stringify(data)};}});
  const text=fs.readFileSync('server/poko_server/ui.py','utf8');
  let script=text.match(/<script>([\s\S]*?)<\/script>/)[1];
  script=script.slice(0,script.lastIndexOf('nav();render();setInterval'));
  vm.runInContext(script+'\nnavigate=(view)=>{active=view;};active="player";',context);
  async function run(code){return vm.runInContext(code,context);}
  async function settle(){for(let i=0;i<30;i++){await new Promise(resolve=>setImmediate(resolve));if(!vm.runInContext('renderInFlight',context))return;}throw Error('render did not settle');}
  return {state,calls,writes,run,settle,html:()=>element('player').innerHTML};
}

test('Play shows selected Starting state, then matching Playing title and artwork',async()=>{
  const d=dashboard();await d.run('playItem("audio","chosen",{title:"Chosen song"})');await d.settle();
  assert.match(d.html(),/Starting/);assert.match(d.html(),/Chosen song/);
  assert(d.calls.some(url=>url.includes('async=true')));
  assert(!d.calls.includes('/api/playback/status'),'do not block on playback state during startup');
  d.state.operation={status:'succeeded',result:{playback:{item_id:'chosen'}}};
  await d.run('render()');await d.settle();
  assert.match(d.html(),/Playing/);assert.match(d.html(),/Chosen artist/);
  assert.match(d.html(),/audio\/chosen\/thumbnail.jpg/);
});

test('token settings show paste instructions and escape device names without exposing credentials',async()=>{
  const d=dashboard();
  const html=await d.run('tokenSettings({items:[{id:"abc123",name:"<script>bad</script>",token:"must-not-render",hash:"must-not-render"}]})');
  assert.match(html,/Device token/);
  assert.match(html,/will not be shown again/);
  assert.match(html,/&lt;script&gt;/);
  assert.doesNotMatch(html,/<script>bad/);
  assert.doesNotMatch(html,/must-not-render/);
  assert.match(html,/id="newDeviceToken" type="password" readonly/);
  assert.match(html,/data-token-revoke="abc123"/);
});

test('failed and mismatched operations never present Playing',async()=>{
  for(const operation of [{status:'failed',result:{error:'Device unavailable'}},{status:'succeeded',result:{playback:{item_id:'different'}}}]){
    const d=dashboard();d.state.operation=operation;
    await d.run('playItem("audio","chosen",{title:"Chosen song"})');await d.settle();
    assert.match(d.html(),/Failed/);assert.doesNotMatch(d.html(),/<h3>Playing/);
    assert.doesNotMatch(d.html(),/different\/thumbnail/);
  }
});

test('an old status response cannot overwrite a newer Play selection',async()=>{
  const d=dashboard();let release;
  d.state.statusGate=new Promise(resolve=>{release=resolve});
  const oldRender=d.run('render()');await new Promise(resolve=>setImmediate(resolve));
  await d.run('playItem("audio","chosen",{title:"Chosen song"})');
  d.writes.length=0;
  release({active:true,item_id:'old',title:'Old song',kind:'audio',position_s:0,mode:'audio_player'});
  await oldRender;await d.settle();
  assert(d.writes.every(html=>!html.includes('Old song')));
  assert.match(d.html(),/Chosen song/);
});

test('poll refreshes coalesce instead of building a FIFO of API requests',async()=>{
  const d=dashboard();let release;
  d.state.statusGate=new Promise(resolve=>{release=resolve});
  const first=d.run('render()');await new Promise(resolve=>setImmediate(resolve));
  for(let i=0;i<32;i++)await d.run('render()');
  assert.equal(d.calls.filter(url=>url==='/api/playback/status').length,1);
  release(d.state.playback);d.state.statusGate=null;
  await first;await d.settle();
  assert.equal(d.calls.filter(url=>url==='/api/playback/status').length,2);
});
