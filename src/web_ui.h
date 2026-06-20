#pragma once
#define MATRIX_WEB_UI_H
#include <pgmspace.h>

// Self-contained control panel (no external CDN — served on the LAN).
static const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MatrixLife</title>
<style>
 :root{color-scheme:dark}
 body{font:15px system-ui,sans-serif;margin:0;background:#0b0d12;color:#e7e9ee}
 header{position:sticky;top:0;background:#12151c;padding:12px 16px;border-bottom:1px solid #232838}
 h1{font-size:16px;margin:0 0 4px}
 .geo{color:#8b93a7;font-size:12px}
 main{padding:16px;max-width:720px;margin:0 auto}
 fieldset{border:1px solid #232838;border-radius:10px;margin:0 0 16px;padding:12px 14px}
 legend{color:#9aa3ba;padding:0 6px;text-transform:uppercase;font-size:11px;letter-spacing:.06em}
 .row{display:grid;grid-template-columns:1fr 130px 64px;gap:10px;align-items:center;margin:8px 0}
 .row label{color:#cdd3e1}
 input[type=range]{width:100%}
 input[type=number]{width:60px;background:#0b0d12;color:#e7e9ee;border:1px solid #2c3346;border-radius:6px;padding:4px}
 .bar{position:sticky;bottom:0;display:flex;gap:8px;flex-wrap:wrap;background:#12151c;padding:12px 16px;border-top:1px solid #232838}
 button{font:inherit;padding:8px 12px;border-radius:8px;border:1px solid #2c3346;background:#1b2031;color:#e7e9ee;cursor:pointer}
 button.primary{background:#2a6df4;border-color:#2a6df4}
 button.warn{background:#3a1d22;border-color:#5a2b33;color:#ffb3bd}
 .stats{color:#8b93a7;font-size:12px;margin-left:auto;align-self:center}
</style></head><body>
<header><h1>MatrixLife control panel</h1>
 <div class="geo" id="geo"></div>
 <div class="geo" id="stats"></div></header>
<main id="groups"></main>
<div class="bar">
 <button class="primary" onclick="post('/api/save')">Save</button>
 <button onclick="post('/api/revert')">Revert to saved</button>
 <button onclick="post('/api/reset')">Reset to defaults</button>
 <button onclick="act('reseed')">Reseed</button>
 <button onclick="act('burn')">Burn</button>
 <button class="warn" onclick="forget()">Forget WiFi</button>
</div>
<script>
let fields=[];
function el(t,a={},...k){const e=document.createElement(t);for(const x in a)e.setAttribute(x,a[x]);e.append(...k);return e;}
function debounce(fn,ms){let h;return(...a)=>{clearTimeout(h);h=setTimeout(()=>fn(...a),ms);};}
const sendField=debounce(async(key,val)=>{await fetch('/api/settings',{method:'POST',headers:{'content-type':'application/x-www-form-urlencoded'},body:key+'='+val});},120);
function build(data){
 document.getElementById('geo').textContent=`panel ${data.geometry.width}×${data.geometry.height} · ${data.geometry.bitDepth}-bit · tile ${data.geometry.tile} (compile-time)`;
 fields=data.settings.fields;
 const byGroup={};fields.forEach(f=>(byGroup[f.group]=byGroup[f.group]||[]).push(f));
 const root=document.getElementById('groups');root.innerHTML='';
 for(const g in byGroup){
  const fs=el('fieldset',{},el('legend',{},g));
  byGroup[g].forEach(f=>{
   const num=el('input',{type:'number',min:f.min,max:f.max,step:f.step,value:f.live});
   const rng=el('input',{type:'range',min:f.min,max:f.max,step:f.step,value:f.live});
   const apply=v=>{v=Math.max(f.min,Math.min(f.max,+v));num.value=v;rng.value=v;sendField(f.key,v);};
   rng.oninput=()=>apply(rng.value);num.oninput=()=>apply(num.value);
   fs.append(el('div',{class:'row'},el('label',{},f.label),rng,num));
  });
  root.append(fs);
 }
}
async function load(){build(await (await fetch('/api/settings')).json());}
async function post(u){build(await (await fetch(u,{method:'POST'})).json());}
async function act(d){await fetch('/api/action',{method:'POST',headers:{'content-type':'application/x-www-form-urlencoded'},body:'do='+d});}
async function forget(){if(confirm('Erase WiFi credentials and reboot into BLE provisioning?')){await fetch('/api/forget-wifi',{method:'POST'});}}
async function stats(){try{const s=await(await fetch('/api/stats')).json();document.getElementById('stats').textContent=`${s.renderFps} fps · ${s.lifeUps} ups · ${s.live} live · gen ${s.generation}`;}catch(e){}}
load();setInterval(stats,1000);
</script></body></html>)HTML";
