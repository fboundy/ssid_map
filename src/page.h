#pragma once


// Single-page UI served at "/". Talks to /api/* with fetch(); no external assets
// so it works with no internet. Text is inserted via textContent to keep SSIDs
// from being interpreted as HTML.
static const char PAGE_HTML[] = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Penrith MRT Xebro</title>
<style>
:root{--bg:#f4f6f8;--card:#fff;--fg:#1c2430;--mut:#6b7684;--line:#dfe4ea;--acc:#0b6bcb;--ok:#1a8f4a;--warn:#c27a00;--bad:#c62828;--chip:#eef2f6}
@media(prefers-color-scheme:dark){:root{--bg:#121720;--card:#1b2230;--fg:#e6ebf2;--mut:#98a3b3;--line:#2c3646;--acc:#5aa8ff;--ok:#4cc27b;--warn:#f0b04a;--bad:#ff6b6b;--chip:#243044}}
*{box-sizing:border-box}body{margin:0;font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:16px;background:var(--card);border-bottom:1px solid var(--line)}header h1{margin:0;font-size:18px}header small{color:var(--mut)}
main{max-width:720px;margin:0 auto;padding:12px 12px 40px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin:12px 0}
.card h2{margin:0 0 10px;font-size:15px;text-transform:uppercase;letter-spacing:.04em;color:var(--mut)}
.badge{display:inline-block;padding:3px 10px;border-radius:999px;font-weight:600;font-size:13px;color:#fff;background:var(--mut)}
.badge.routing{background:var(--ok)}.badge.countdown,.badge.connecting,.badge.retry{background:var(--warn)}.badge.override,.badge.hold{background:var(--bad)}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.grow{flex:1 1 auto}
button{font:inherit;padding:7px 12px;border-radius:7px;border:1px solid var(--line);background:var(--chip);color:var(--fg);cursor:pointer}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}button.danger{color:var(--bad)}button:disabled{opacity:.5;cursor:default}
input,select{font:inherit;padding:7px 9px;border-radius:7px;border:1px solid var(--line);background:var(--bg);color:var(--fg);min-width:0}
table{width:100%;border-collapse:collapse}td,th{padding:8px 6px;border-bottom:1px solid var(--line);text-align:left;vertical-align:middle}th{color:var(--mut);font-weight:600;font-size:12px;text-transform:uppercase}
.sig{display:inline-block;width:52px;height:8px;background:var(--line);border-radius:4px;overflow:hidden;vertical-align:middle;margin-right:6px}.sig i{display:block;height:100%;background:var(--ok)}
.mut{color:var(--mut)}.small{font-size:13px}.tag{display:inline-block;padding:1px 7px;border-radius:5px;background:var(--chip);font-size:12px;margin-left:4px}
.tag.ov{background:var(--bad);color:#fff}.tag.sel{background:var(--ok);color:#fff}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:var(--fg);color:var(--bg);padding:9px 14px;border-radius:8px;opacity:0;transition:opacity .2s;pointer-events:none;max-width:90vw}
#toast.show{opacity:1}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;margin:8px 0 0}dt{color:var(--mut)}dd{margin:0;word-break:break-all}
.alert{padding:10px 12px;border-radius:8px;background:var(--chip);margin-top:10px}.alert.bad{border-left:4px solid var(--bad)}.alert.ok{border-left:4px solid var(--ok)}.alert.warn{border-left:4px solid var(--warn)}
</style></head><body>
<header><h1>Penrith MRT Xebro</h1><small id="apinfo">&nbsp;</small></header>
<main>

<section class="card">
  <h2>Status</h2>
  <div class="row"><span id="mode" class="badge">…</span><span id="modeText" class="grow"></span></div>
  <div id="alert"></div>
  <dl id="sta"></dl>
  <div class="row" style="margin-top:12px">
    <button class="pri" id="btnConnect" onclick="api('/api/connect')">Connect now</button>
    <button id="btnDisconnect" onclick="api('/api/disconnect')">Disconnect</button>
    <button id="btnScan" onclick="api('/api/scan')">Rescan</button>
  </div>
</section>

<section class="card">
  <h2>Available networks <span id="scanInfo" class="mut small" style="text-transform:none;letter-spacing:0"></span></h2>
  <table><thead><tr><th>SSID</th><th>Signal</th><th></th></tr></thead><tbody id="scan"></tbody></table>
</section>

<section class="card">
  <h2>Selected networks</h2>
  <p class="mut small" style="margin:0 0 8px">The device joins the strongest of these and routes traffic through it.</p>
  <table><tbody id="saved"></tbody></table>
  <div class="row" style="margin-top:10px">
    <input id="addSsid" class="grow" placeholder="SSID (hidden or not in scan)" maxlength="32">
    <input id="addPass" placeholder="Password" type="password" maxlength="63">
    <button onclick="saveNet(v('addSsid'),v('addPass'))">Add</button>
  </div>
</section>

<section class="card">
  <h2>Over-ride SSIDs</h2>
  <p class="mut small" style="margin:0 0 8px">If any enabled over-ride SSID is in range, NAT is disabled and the uplink is dropped until it disappears.</p>
  <table><tbody id="overrides"></tbody></table>
  <div class="row" style="margin-top:10px">
    <select id="ovPick" class="grow"><option value="">Pick from scan…</option></select>
    <input id="ovManual" class="grow" placeholder="or type an SSID" maxlength="32">
    <button onclick="addOverride()">Add over-ride</button>
  </div>
</section>

<section class="card">
  <h2>Access point</h2>
  <div class="row">
    <label class="mut" for="apnet">Network (/24)</label>
    <input id="apnet" class="grow" placeholder="192.168.137.0">
    <button onclick="setApNet()">Save</button>
    <button class="danger" onclick="if(confirm('Reboot the device now?'))api('/api/reboot')">Reboot</button>
  </div>
  <p class="mut small" style="margin:8px 0 0">Device takes .1, DHCP leases start at .10. Changes apply after reboot.</p>
</section>

<p class="mut small" id="foot"></p>
</main>
<div id="toast"></div>
<script>
const $=id=>document.getElementById(id), v=id=>$(id).value.trim();
let S=null, lastStatusOk=true;
function el(tag,attrs,...kids){const e=document.createElement(tag);for(const k in attrs||{}){if(k==='onclick')e.onclick=attrs[k];else if(k==='class')e.className=attrs[k];else e.setAttribute(k,attrs[k]);}for(const k of kids){if(k==null)continue;e.append(k.nodeType?k:document.createTextNode(k));}return e;}
function toast(m,bad){const t=$('toast');t.textContent=m;t.style.background=bad?'var(--bad)':'';t.style.color=bad?'#fff':'';t.classList.add('show');clearTimeout(t._h);t._h=setTimeout(()=>t.classList.remove('show'),2600);}
async function api(path,params){try{const body=new URLSearchParams(params||{});const r=await fetch(path,{method:'POST',body});const j=await r.json();toast(j.msg||(j.ok?'OK':'Failed'),!j.ok);setTimeout(refresh,300);return j.ok;}catch(e){toast('Request failed',true);return false;}}
function saveNet(ssid,pass){if(!ssid)return toast('SSID required',true);api('/api/save',{ssid,pass}).then(ok=>{if(ok){$('addSsid').value='';$('addPass').value='';}});}
function addOverride(){const s=v('ovManual')||v('ovPick');if(!s)return toast('Choose or type an SSID',true);api('/api/override',{ssid:s,enabled:1}).then(ok=>{if(ok){$('ovManual').value='';$('ovPick').value='';}});}
function setApNet(){api('/api/apnet',{net:v('apnet')});}
function sigPct(r){return Math.max(0,Math.min(100,Math.round((r+95)*100/60)));}
function sig(r){return el('span',{},el('span',{class:'sig'},el('i',{style:'width:'+sigPct(r)+'%'})),el('span',{class:'mut small'},r+' dBm'));}
const MODE_TEXT={waiting:'No networks selected — waiting for a selection.',countdown:'Auto-connect to the strongest selected network in',connecting:'Joining',routing:'Routing AP clients via',retry:'Looking for a selected network…',hold:'Disconnected by user. Press Connect now to resume.',override:'Over-ride SSID in range — NAT disabled.'};
function render(s){
  S=s;
  $('apinfo').textContent=s.ap.ssid+' · '+s.ap.ip+' · '+s.ap.clients+' client'+(s.ap.clients==1?'':'s');
  const m=$('mode');m.textContent=s.mode.toUpperCase();m.className='badge '+s.mode;
  let t=MODE_TEXT[s.mode]||'';
  if(s.mode==='countdown')t+=' '+Math.ceil(s.countdown/1000)+'s';
  if(s.mode==='connecting'||s.mode==='routing')t+=' "'+(s.sta.ssid||s.target)+'"';
  if(s.mode==='override')t+=' ("'+s.override+'")';
  $('modeText').textContent=t;
  const a=$('alert');a.innerHTML='';
  if(s.rebooting)a.append(el('div',{class:'alert warn'},'Rebooting…'));
  else if(s.error)a.append(el('div',{class:'alert bad'},s.error));
  else if(s.nat)a.append(el('div',{class:'alert ok'},'NAT active — clients of "'+s.ap.ssid+'" have internet via "'+s.sta.ssid+'".'));
  const d=$('sta');d.innerHTML='';
  if(s.sta.connected){[['Uplink',s.sta.ssid],['IP',s.sta.ip],['Gateway',s.sta.gw],['DNS',s.sta.dns],['RSSI',s.sta.rssi+' dBm']].forEach(([k,x])=>{d.append(el('dt',{},k),el('dd',{},x));});}
  $('btnConnect').disabled=!s.saved.length||!!s.override;
  $('btnDisconnect').disabled=!(s.mode==='routing'||s.mode==='connecting'||s.mode==='countdown'||s.mode==='retry');
  $('btnScan').disabled=s.scanning;
  $('scanInfo').textContent=s.scanning?'scanning…':(s.scanAge<0?'no scan yet':'updated '+Math.round(s.scanAge/1000)+'s ago');
  const tb=$('scan');tb.innerHTML='';
  if(!s.scan.length)tb.append(el('tr',{},el('td',{colspan:3,class:'mut'},s.scanning?'Scanning…':'Nothing found yet')));
  for(const n of s.scan){
    const name=el('span',{},n.ssid,n.secure?el('span',{class:'mut'},' 🔒'):null,n.saved?el('span',{class:'tag sel'},'selected'):null,n.override?el('span',{class:'tag ov'},'over-ride'):null);
    let act;
    if(n.saved){act=el('div',{class:'row'},el('button',{onclick:()=>api('/api/connect',{ssid:n.ssid}),disabled:s.override?'':null},'Connect'),el('button',{class:'danger',onclick:()=>api('/api/remove',{ssid:n.ssid})},'Remove'));}
    else{const p=el('input',{type:'password',placeholder:n.secure?'Password':'(open)',maxlength:63,style:'width:130px'});act=el('div',{class:'row'},p,el('button',{class:'pri',onclick:()=>saveNet(n.ssid,p.value)},'Select'));}
    if(!n.override)act.append(el('button',{onclick:()=>api('/api/override',{ssid:n.ssid,enabled:1}),title:'Add as over-ride SSID'},'+Over-ride'));
    tb.append(el('tr',{},el('td',{},name),el('td',{},sig(n.rssi)),el('td',{},act)));
  }
  const sv=$('saved');sv.innerHTML='';
  if(!s.saved.length)sv.append(el('tr',{},el('td',{class:'mut'},'None selected. Pick networks from the scan above.')));
  for(const n of s.saved){
    const seen=s.scan.find(x=>x.ssid===n.ssid);
    const p=el('input',{type:'password',placeholder:n.hasPass?'(change password)':'(open / no password)',maxlength:63,style:'width:150px'});
    sv.append(el('tr',{},
      el('td',{},el('div',{},n.ssid,(s.sta.connected&&s.sta.ssid===n.ssid)?el('span',{class:'tag sel'},'connected'):null),el('div',{class:'mut small'},seen?('in range, '+seen.rssi+' dBm'):'not in range')),
      el('td',{},el('div',{class:'row'},p,el('button',{onclick:()=>saveNet(n.ssid,p.value)},'Save'),el('button',{onclick:()=>api('/api/connect',{ssid:n.ssid}),disabled:(seen&&!s.override)?null:''},'Connect'),el('button',{class:'danger',onclick:()=>api('/api/remove',{ssid:n.ssid})},'Remove')))));
  }
  const ov=$('overrides');ov.innerHTML='';
  if(!s.overrides.length)ov.append(el('tr',{},el('td',{class:'mut'},'No over-ride SSIDs.')));
  for(const o of s.overrides){
    const seen=s.scan.find(x=>x.ssid===o.ssid);
    const cb=el('input',{type:'checkbox'});cb.checked=o.enabled;cb.onchange=()=>api('/api/override',{ssid:o.ssid,enabled:cb.checked?1:0});
    ov.append(el('tr',{},
      el('td',{},el('label',{class:'row'},cb,el('span',{},o.ssid,seen?el('span',{class:'tag ov'},'in range'):null)),el('div',{class:'mut small'},o.enabled?'enabled':'disabled')),
      el('td',{style:'text-align:right'},el('button',{class:'danger',onclick:()=>api('/api/override/remove',{ssid:o.ssid})},'Remove'))));
  }
  const pick=$('ovPick');const cur=pick.value;pick.innerHTML='';pick.append(el('option',{value:''},'Pick from scan…'));
  for(const n of s.scan){if(!s.overrides.find(o=>o.ssid===n.ssid))pick.append(el('option',{value:n.ssid},n.ssid));}
  pick.value=cur;
  if(document.activeElement!==$('apnet')&&!$('apnet').value)$('apnet').value=s.ap.net;
  $('foot').textContent='Uptime '+Math.floor(s.uptime/60)+' min · free heap '+Math.round(s.heap/1024)+' KB';
}
function typing(){const a=document.activeElement;return a&&(a.tagName==='INPUT'||a.tagName==='SELECT')&&a.type!=='checkbox';}
async function refresh(){try{const r=await fetch('/api/status',{cache:'no-store'});const s=await r.json();lastStatusOk=true;if(typing())return;render(s);}catch(e){if(lastStatusOk){$('modeText').textContent='Lost contact with device…';}lastStatusOk=false;}}
refresh();setInterval(refresh,3000);
</script>
</body></html>
)HTML";
