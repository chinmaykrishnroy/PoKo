const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync('PokoWebUI.cpp','utf8');
const handlers=source.slice(source.indexOf('async function loadTunerSettings()'),source.indexOf('let healthPollTimer=null;'));

function fixture(api){
  const reference={name:'reference_hz',value:'440',disabled:true};
  const profile={name:'profile',value:'guitar',disabled:true};
  const button={disabled:true};
  const elements=[reference,profile,button];elements.reference_hz=reference;elements.profile=profile;
  const form={elements,isConnected:true};const result={textContent:''};
  const dom={tunerSettingsForm:form,tunerResult:result};
  class FormData {
    constructor(form){this.values=form.elements.filter(x=>x.name&&!x.disabled).map(x=>[x.name,x.value]);}
    [Symbol.iterator](){return this.values[Symbol.iterator]();}
  }
  const context={api,$:id=>dom[id]||null,FormData,URLSearchParams};vm.createContext(context);vm.runInContext(handlers,context);
  return {context,form,reference,profile,button,result,dom};
}
test('loads saved tuning before enabling editing',async()=>{
  const f=fixture(async()=>({reference_hz:442.2,profile:'keyboard'}));
  await f.context.loadTunerSettings();
  assert.equal(f.reference.value,'442.2');assert.equal(f.profile.value,'keyboard');
  assert.ok(f.form.elements.every(x=>!x.disabled));
});
test('load failure keeps unknown settings disabled',async()=>{
  const f=fixture(async()=>{throw Error('Unavailable');});await f.context.loadTunerSettings();
  assert.ok(f.form.elements.every(x=>x.disabled));assert.equal(f.result.textContent,'Unavailable');
});
test('an obsolete settings request cannot change a newly opened view',async()=>{
  let finish;const f=fixture(()=>new Promise(resolve=>{finish=resolve;}));
  const request=f.context.loadTunerSettings();f.form.isConnected=false;
  finish({reference_hz:432,profile:'keyboard'});await request;
  assert.equal(f.reference.value,'440');assert.equal(f.result.textContent,'');
});
test('save captures fields before disabling them and sends no playback controls',async()=>{
  let request;const f=fixture(async(path,options)=>{request={path,options};return {reference_hz:441.2,profile:'keyboard'};});
  f.form.elements.forEach(x=>x.disabled=false);f.reference.value='441.2';f.profile.value='keyboard';
  await f.context.saveTunerSettings({currentTarget:f.form,preventDefault(){}});
  assert.equal(request.path,'/api/acoustic/settings');assert.equal(request.options.method,'POST');
  assert.equal(request.options.body.toString(),'reference_hz=441.2&profile=keyboard');
  assert.ok(f.form.elements.every(x=>!x.disabled));assert.equal(f.result.textContent,'Tuner settings updated.');
});
test('save failure permits retry and preserves entered values',async()=>{
  const f=fixture(async()=>{throw Error('Try again');});f.form.elements.forEach(x=>x.disabled=false);f.reference.value='432';
  await f.context.saveTunerSettings({currentTarget:f.form,preventDefault(){}});
  assert.equal(f.reference.value,'432');assert.equal(f.result.textContent,'Try again');assert.ok(!f.button.disabled);
});
test('finishing a save after navigation is harmless',async()=>{
  let finish;const f=fixture(()=>new Promise(resolve=>{finish=resolve;}));f.form.elements.forEach(x=>x.disabled=false);
  const request=f.context.saveTunerSettings({currentTarget:f.form,preventDefault(){}});f.form.isConnected=false;
  finish({reference_hz:442,profile:'keyboard'});await request;assert.equal(f.result.textContent,'');
});
