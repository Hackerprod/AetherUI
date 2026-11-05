import assert from "node:assert/strict";
import { types as nodeTypes } from "node:util";
import { createNativeApi, loadKoffi } from "../src/ffi.mjs";
import { NodeView, LayoutBox, DisplayList } from "../src/index.mjs";
const ffi = loadKoffi();
const signatures = new Map();
const api = createNativeApi({
  ffi,
  library: {
    func: (name, result, args) => {
      signatures.set(name, { result, args });
      return () => { throw new Error("Aether must not be called by this shim test"); };
    },
  },
});
const prim = (type) => ffi.introspect(type).primitive;
const getters = [
  "aether_node_get_text_content", "aether_element_get_tag_name",
  "aether_element_get_attribute", "aether_element_get_attribute_name",
  "aether_element_get_attribute_value", "aether_layout_box_get_text",
];
for (const getter of getters) {
  assert.equal(prim(signatures.get(getter).result), "Pointer", getter);
  assert.equal(ffi.introspect(signatures.get(getter).result).disposable, false, getter);
}
assert.equal(prim(signatures.get('aether_string_free').args[0]),'Pointer');
assert.equal(prim(signatures.get('aether_status_message').result), 'String');
for(const [type,field] of [['DrawText','text'],['DrawTextGradient','text'],['DrawTextShadow','text'],['DrawBackgroundImage','source'],['BackgroundLayer','image_source']]) {
 assert.equal(prim(ffi.introspect(api.types[type]).members[field].type),'String');
}
const lib=ffi.load(process.argv[2]);
// Exercise the private owned-string helper through its public NodeView getter.
const readOwned = (api, pointer) => new NodeView({
  _api: { ...api, functions: { ...api.functions, nodeGetTextContent: () => pointer } },
  _generation: 0, _assertAlive: () => true,
}, {}, 0, false).textContent;
const alloc=lib.func('review_alloc',api.types.OwnedUtf8Pointer,['str']);
const release=lib.func('review_free','void',[api.types.OwnedUtf8Pointer]);
const counter = (name) => lib.func(name, "int", [])();
const snapshot = () => ({
  alloc: counter("review_allocated"),
  free: counter("review_released"),
  invalid: counter("review_invalid_free"),
  commands: counter("review_command_frees"),
});
let decodes = 0, frees = 0;
const operations = [];
const ffiproxy = {
  ...ffi,
  decode: (...args) => {
    decodes++;
    operations.push(["decode", ...args]);
    return ffi.decode(...args);
  },
};
const ownedApi = {
  ffi: ffiproxy,
  functions: {
    stringFree: (pointer) => {
      frees++;
      operations.push(["free", pointer]);
      release(pointer);
    },
  },
};
for(const text of ['', 'ASCII','árbol 中文 🙂','https://ejemplo.test/ñ/图片🙂.png']) {
 let pointer=alloc(text); assert(nodeTypes.isExternal(pointer));
 const b=snapshot(); assert.equal(readOwned(ownedApi,pointer),text);
 assert.equal(snapshot().free,b.free+1);
 assert.deepEqual(operations.slice(-2), [["decode",pointer,"char",-1],["free",pointer]]);
 assert.equal(ffi.decode(Buffer.from(text+'\0'),'char',-1),text);
}
let d=decodes,f=frees;
assert.equal(readOwned(ownedApi,null),null);
assert.equal(decodes, d);
assert.equal(frees, f);
for(const notPointer of ['', 'not an External', {}, 2, 2n]) assert.throws(()=>readOwned(ownedApi,notPointer),/Expected a raw owned UTF-8 pointer/);
assert.equal(decodes, d);
assert.equal(frees, f);
const failPtr=alloc('decode failure');
assert.throws(()=>readOwned({ffi:{decode:()=>{throw new Error('injected JS decode error');}},functions:ownedApi.functions},failPtr),/injected JS decode error/);
assert.equal(frees,f+1);
const events = [];
// All six public wrappers consume one allocation per getter call.
Object.assign(ownedApi.functions,{
 nodeGetTextContent:()=>alloc('text árbol 中文 🙂'),
 elementGetTagName:()=>alloc('section'),
 elementGetAttribute:(_p,name)=>name==='missing'?null:alloc(name==='empty'?'':'value 🙂'),
 elementGetAttributeCount:()=>2,
 elementGetAttributeName:(_p,i)=>{events.push('name'+i);return alloc('ñ'+i);},
 elementGetAttributeValue:(_p,i)=>{events.push('value'+i);return alloc('中文'+i);},
 layoutBoxGetText:()=>alloc('layout 🙂'),
 stringFree:p=>{events.push('free');frees++;release(p);},
});
const doc={_api:ownedApi,_generation:0,_assertAlive:()=>true};
const view=new NodeView(doc,{},0,true);
assert.equal(view.textContent, "text árbol 中文 🙂");
assert.equal(view.tagName, "section");
assert.equal(view.getAttribute("missing"), null);
assert.equal(view.getAttribute("empty"), "");
assert.equal(view.getAttribute("found"), "value 🙂");
events.length=0;
assert.deepEqual(view.attributes,[{name:'ñ0',value:'中文0'},{name:'ñ1',value:'中文1'}]);
assert.deepEqual(events,['name0','free','value0','free','name1','free','value1','free']);
const box=new LayoutBox({_api:ownedApi,_assertAlive:()=>true},{});
assert.equal(box.text,'layout 🙂');
for(let i=0;i<2000;i++) assert.equal(view.textContent,'text árbol 中文 🙂');
console.log('PASS owned: raw External + direct-byte UTF-8, NULL/empty, rejects strings, frees on JS exception; all 6 wrappers; 2000 repeats.');
// Paint fields are copied once and released by their container, never stringFree.
const paint=lib.func('review_paint','int',['int','str','void *']);
const paintFree=lib.func('review_paint_free','void',['void *']);
const paintSize=lib.func('review_paint_size','size_t',[])();
assert.equal(paintSize,ffi.sizeof(api.types.PaintCommand));
assert.equal(lib.func('review_paint_offset','size_t',[])(),api.types.PaintDataOffset);
api.abi={paintCommandSize:paintSize};
let tag=4,text='paint árbol 中文 🙂';
Object.assign(api.functions,{
 displayListGetCount:()=>1,displayListGetCommand:(_h,_i,buffer)=>paint(tag,text,buffer),
 paintCommandFree:paintFree,displayListDestroy:()=>{},
 stringFree:()=>{throw Error('Paint fields must never call stringFree');},
});
const list=new DisplayList(api,{},null);
const copied=[];
for(tag of [4,5,9,10]) for(text of [null,'','paint árbol 中文 🙂','https://ejemplo.test/ñ/图片🙂.png']) {
 const before=snapshot(); const cmd=list.command(0); copied.push([cmd,tag,text]);
 assert.equal(tag===10?cmd.source:cmd.text,text);
 assert.equal(snapshot().commands,before.commands+1);
 assert.equal(snapshot().free,before.free+(text===null?0:1));
 if(tag===4) assert.equal(cmd.fontFamily,'á字体🙂');
}
for(tag of [4,5,9,10]) {
 const original=api.ffi;
 api.ffi={...ffi,decode:(...args)=>{if(args.length===3)throw new Error('injected paint decode error');return ffi.decode(...args);}};
 const before=snapshot();assert.throws(()=>list.command(0),/injected paint decode error/);
 assert.equal(snapshot().commands,before.commands+1);assert.equal(snapshot().free,before.free+1);
 api.ffi=original;
}
list.dispose();for(const [cmd,t,s] of copied) assert.equal(t===10?cmd.source:cmd.text,s);
console.log('PASS paint: tags 4/5/9/10, null/empty/Unicode/URL, strings copied before command free, one command free on success and JS exceptions; snapshots survive disposal.');
// Borrowed strings remain ordinary JS values after native storage is released.
const bg=lib.func('review_background','void',['str','void *']);
const bgfree=lib.func('review_background_free','void',['void *']);
assert.equal(lib.func('review_background_size','size_t',[])(),ffi.sizeof(api.types.BackgroundLayer));
const buffer=Buffer.alloc(ffi.sizeof(api.types.BackgroundLayer));
bg('https://ejemplo.test/ñ/图片🙂.png',buffer);
const layer=ffi.decode(buffer,api.types.BackgroundLayer);bgfree(buffer);
assert.equal(layer.image_source,'https://ejemplo.test/ñ/图片🙂.png');
let callbackText,diagnostic;
const cb=ffi.register((text,_style,width)=>{callbackText=text;return {width,height:0,baseline:0};},api.types.TextMeasurerPointer);
try{lib.func('review_text_callback','void',[api.types.TextMeasurerPointer])(cb);}finally{ffi.unregister(cb);}
assert.equal(callbackText,'árbol 中文 🙂');
const cb2=ffi.register((...args)=>diagnostic=args,api.types.StyleDiagnosticPointer);
try{lib.func('review_diagnostic_callback','void',[api.types.StyleDiagnosticPointer])(cb2);}finally{ffi.unregister(cb2);}
assert.deepEqual(diagnostic,['árbol 中文 🙂','árbol 中文 🙂','árbol 中文 🙂','árbol 中文 🙂',null]);
assert.equal(lib.func('review_status','str',[])(),'literal estático 中文 🙂');
console.log('PASS borrowed: BackgroundLayer image_source, text callback and 4 diagnostic strings retain copies after shim releases native storage; static status String.');
assert.deepEqual(snapshot(),{alloc:snapshot().alloc,free:snapshot().alloc,invalid:0,commands:20});
console.log('PASS lifetime counters: '+JSON.stringify(snapshot()));
