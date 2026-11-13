// Web page served by the ESP32 (self-contained, works without internet).
#pragma once
#include <pgmspace.h>

static const char PAGE_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kühlschrank</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1b1f24;--mut:#6a737d;--line:#e3e6ea;--acc:#1f6feb;--c0:#1f6feb;--c1:#8250df;--bad:#cf222e}
@media (prefers-color-scheme:dark){:root{--bg:#0d1117;--card:#161b22;--fg:#e6edf3;--mut:#8b949e;--line:#30363d;--acc:#58a6ff;--c0:#58a6ff;--c1:#bc8cff;--bad:#ff7b72}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:760px;margin:0 auto;padding:16px}
h1{font-size:22px;margin:4px 0 14px}
h2{font-size:17px;margin:0 0 10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:14px}
.top{display:flex;justify-content:space-between;align-items:baseline;gap:8px;flex-wrap:wrap}
.name{font-weight:600}
.temp{font-size:44px;font-weight:700;letter-spacing:-1px;font-variant-numeric:tabular-nums}
.mut{color:var(--mut);font-size:14px}
.stats{display:flex;gap:16px;flex-wrap:wrap}
canvas{width:100%;height:170px;display:block;margin-top:8px}
.scr{overflow-x:auto;overflow-y:hidden;margin-top:8px;-webkit-overflow-scrolling:touch;scrollbar-width:thin;overscroll-behavior-x:contain}
.track{height:170px}
.scr canvas{position:sticky;left:0;margin:0}
.head{display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:14px}
.head h1{margin:4px 0}
.tools{display:flex;gap:8px;align-items:center}
.tools button{margin:0;padding:6px 12px;font-size:14px}
.seg{display:flex;border:1px solid var(--line);border-radius:8px;overflow:hidden}
.seg button{border-radius:0;background:transparent;color:var(--fg)}
.seg button.on{background:var(--acc);color:#fff}
label{display:block;font-size:14px;color:var(--mut);margin:10px 0 4px}
input,select{width:100%;padding:9px 10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg);font:inherit}
button{padding:9px 14px;border:0;border-radius:8px;background:var(--acc);color:#fff;font:inherit;cursor:pointer;margin:10px 8px 0 0}
button.sec{background:transparent;color:var(--acc);border:1px solid var(--line)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
@media (max-width:520px){.row{grid-template-columns:1fr}}
details summary{cursor:pointer;font-weight:600}
label.chk{display:flex;gap:8px;align-items:flex-start;color:var(--fg);font-size:15px}
label.chk input{width:auto;margin-top:3px}
button.bad{color:var(--bad);border-color:var(--bad)}
.msg{font-size:14px;margin-top:8px;min-height:1em}
.bad{color:var(--bad)}
table{border-collapse:collapse;width:100%;font-size:14px}
td{padding:3px 0;vertical-align:top}td:first-child{color:var(--mut);width:45%}
</style>
</head>
<body>
<main>
<div class="head"><h1>Kühlschrank</h1>
<div class="tools"><div class="seg"><button id="m24" class="on" onclick="setMode('24h')">24 h</button><button id="mall" onclick="setMode('all')">Alles</button></div>
<button class="sec" onclick="downloadCsv()" title="Verlauf als CSV-Datei (Excel) herunterladen">⤓ CSV</button></div></div>
<div id="nosensor" class="card" hidden><b>Keine Fühler gefunden.</b><p class="mut">Verkabelung prüfen: Fühler-VCC an RX2 (GPIO 16), DAT an D4, GND an GND. Die Suche läuft alle 30 Sekunden.</p></div>
<div id="sensors"></div>

<div class="card">
<details>
<summary>Einstellungen</summary>
<div class="row">
<div><label for="name0">Name Fühler 1</label><input id="name0" maxlength="24"></div>
<div><label for="name1">Name Fühler 2</label><input id="name1" maxlength="24"></div>
</div>
<label for="bright">Helligkeit Anzeige</label>
<input id="bright" type="range" min="0" max="7">
<button onclick="saveSettings()">Speichern</button>
<button class="sec" onclick="swap()">Fühler 1 ⇄ 2 tauschen</button>
<div class="msg" id="setmsg"></div>
<p class="mut">Neue oder getauschte Fühler werden automatisch erkannt. Ein ersetzter Fühler übernimmt den Platz des fehlenden.</p>
<button class="sec" onclick="forget(0)">Fühler 1 entfernen</button>
<button class="sec" onclick="forget(1)">Fühler 2 entfernen</button>
</details>
</div>

<div class="card">
<details>
<summary>WLAN</summary>
<table id="wifi"></table>
<p class="mut">Optional: mit dem Heim-WLAN verbinden. Dann ist die Seite im Heimnetz erreichbar (die Anzeige zeigt die Adresse, oder http://kuehlschrank.local/) und die Uhrzeit kommt aus dem Internet.</p>
<label for="ssid">Heim-WLAN</label>
<select id="nets" onchange="if(this.value)ssid.value=this.value"><option value="">– Netz auswählen –</option></select>
<input id="ssid" placeholder="Name (SSID)" style="margin-top:6px">
<label for="pass">Passwort</label>
<input id="pass" type="password">
<button onclick="saveWifi()">Verbinden</button>
<button class="sec" onclick="scan()">Netze suchen</button>
<button class="sec" onclick="ssid.value='';pass.value='';saveWifi()">Heim-WLAN trennen</button>
<div class="msg" id="wifimsg"></div>
<h2 style="margin-top:18px">Hotspot</h2>
<p class="mut" id="defpass"></p>
<label class="chk"><input id="apauto" type="checkbox"> Hotspot nur zur Einrichtung: aus, solange das Heim-WLAN verbunden ist</label>
<p class="mut">Er geht etwa 1 Minute nach dem Verbinden aus, sobald kein Handy mehr mit ihm verbunden ist. Fällt das Heim-WLAN länger als 2 Minuten aus, geht er von selbst wieder an.</p>
<label for="appass">Neues Hotspot-Passwort (8–63 Zeichen, leer = unverändert)</label>
<input id="appass" type="password" autocomplete="new-password">
<button onclick="saveAp()">Hotspot speichern</button>
<div class="msg" id="apmsg"></div>
</details>
</div>

<div class="card">
<details>
<summary>Info &amp; Update</summary>
<table id="info"></table>
<label for="fw">Firmware-Datei (kuehlschrank-update.bin)</label>
<input id="fw" type="file" accept=".bin">
<button onclick="upload()">Firmware aktualisieren</button>
<div class="msg" id="fwmsg"></div>
<h2 style="margin-top:18px">Werkseinstellungen</h2>
<p class="mut">Löscht Heim-WLAN, Hotspot-Passwort, Fühler-Namen und Helligkeit und startet neu. Danach gilt wieder: WLAN „Kuehlschrank“, Passwort „kuehlschrank“. Geht auch ohne Handy: BOOT-Taste 10 Sekunden halten.</p>
<button class="sec bad" id="resetbtn" onclick="factoryReset()">Auf Werkseinstellungen zurücksetzen</button>
<div class="msg" id="resetmsg"></div>
</details>
</div>
</main>

<script>
const $=id=>document.getElementById(id);
const COLORS=['--c0','--c1'];
let state=null,hist=null,formFilled=false;
// Chart: '24h' shows one day per screen width (swipe for older values), 'all' fits everything.
const DAY=1440,CL=34,CR=6,CT=6,CB=20,CH=170,WD=['So','Mo','Di','Mi','Do','Fr','Sa'];
let mode='24h';
const views=[{e:0},{e:0}];  // e = minutes between the newest value and the right edge

function css(v){return getComputedStyle(document.documentElement).getPropertyValue(v).trim()}
function fmt(t,d=1){return t==null?'–':t.toFixed(d).replace('.',',')+' °C'}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
// Every POST carries X-FM: 1; the ESP rejects requests without it (blocks other web pages).
function post(url,data){
  const body=new URLSearchParams(data||{});
  return fetch(url,{method:'POST',body,headers:{'X-FM':'1'}}).then(r=>r.json());
}

function render(){
  if(!state)return;
  const box=$('sensors');
  state.sensors.forEach((s,i)=>{
    let el=$('s'+i);
    if(!el){
      el=document.createElement('div');el.className='card';el.id='s'+i;
      el.innerHTML='<div class="top"><div><div class="name"></div><div class="mut sub"></div></div><div class="temp"></div></div>'+
        '<div class="stats mut"></div><div class="scr"><div class="track"><canvas></canvas></div></div>';
      box.appendChild(el);
      const scr=el.querySelector('.scr'),v=views[i];
      scr.addEventListener('scroll',()=>{
        if(!v.p)return;
        v.e=Math.max(0,Math.min(v.extra,(v.max-scr.scrollLeft)/v.p));
        if(!v.raf)v.raf=requestAnimationFrame(()=>{v.raf=0;drawCard(i)});
      },{passive:true});
    }
    el.style.display=s.configured?'':'none';
    el.querySelector('.name').textContent=s.name||('Fühler '+(i+1));
    el.querySelector('.sub').innerHTML=s.present?(s.t==null?'<span class="bad">keine Messwerte</span>':'Fühler '+(i+1)):'<span class="bad">Fühler nicht gefunden</span>';
    el.querySelector('.temp').textContent=fmt(s.t);
    renderHistory(i,el);
  });
  $('nosensor').hidden=state.sensors.some(s=>s.configured);
  if(!formFilled){
    fillNames();$('bright').value=state.bright;
    $('ssid').value=state.sta.ssid;$('apauto').checked=state.ap.auto;formFilled=true;
  }
  const st=state.sta;
  $('defpass').innerHTML=state.ap.defpass?'<span class="bad">Der Hotspot nutzt noch das Standard-Passwort „kuehlschrank“. Bitte unten ein eigenes setzen.</span>':'';
  $('wifi').innerHTML=
    `<tr><td>Hotspot</td><td>${esc(state.ap.ssid)} · ${state.ap.active?`an · http://${state.ap.ip}/ · ${state.ap.clients} Gerät(e)`:'aus (Heim-WLAN verbunden)'}</td></tr>`+
    `<tr><td>Heim-WLAN</td><td>${st.ssid?esc(st.ssid)+(st.connected?` · verbunden · http://${st.ip}/ · ${st.rssi} dBm`:' · <span class="bad">nicht verbunden</span>'):'nicht eingerichtet'}</td></tr>`;
  const up=state.up,d=Math.floor(up/86400),h=Math.floor(up%86400/3600),m=Math.floor(up%3600/60);
  $('info').innerHTML=
    `<tr><td>Firmware</td><td>${esc(state.fw)}</td></tr>`+
    `<tr><td>Laufzeit</td><td>${d?d+' T ':''}${h} h ${m} min</td></tr>`+
    `<tr><td>Uhrzeit</td><td>${state.epoch?new Date(state.epoch*1000).toLocaleString('de-DE'):'unbekannt'}</td></tr>`+
    state.sensors.map((s,i)=>`<tr><td>Fühler ${i+1}</td><td>${s.addr||'–'}</td></tr>`).join('');
}

function pad(x){return String(x).padStart(2,'0')}
function hm(d){return pad(d.getHours())+':'+pad(d.getMinutes())}
function endMs(){return hist&&hist.epoch?(hist.epoch-hist.age)*1000:0}  // time of the newest value
function hrs(m){return Math.round(m/60)+' h'}

function setMode(m){
  mode=m;$('m24').classList.toggle('on',m=='24h');$('mall').classList.toggle('on',m=='all');
  views.forEach(v=>{v.e=0;v.tw=-1});render();
}

function renderHistory(i,el){
  const v=views[i],scr=el.querySelector('.scr'),track=el.querySelector('.track');
  v.el=el;
  if(!hist||!hist.data[i]){el.querySelector('.stats').textContent='';return}
  const n=hist.data[i].length,W=scr.clientWidth;
  if(!W)return;
  const V=mode=='all'?Math.max(n,DAY):DAY,extra=Math.max(0,n-V);
  v.V=V;v.extra=extra;v.W=W;v.p=(W-CL-CR)/(V-1);v.max=Math.round(extra*v.p);
  if(v.e>extra)v.e=extra;
  // Only touch the scroll position when the width changes (keeps a running swipe smooth).
  if(v.tw!==W+v.max){v.tw=W+v.max;track.style.width=v.tw+'px';scr.scrollLeft=Math.round(v.max-v.e*v.p)}
  drawCard(i);
}

function drawCard(i){
  const v=views[i],el=v.el,raw=hist.data[i],n=raw.length,stats=el.querySelector('.stats');
  const k0=Math.max(0,Math.ceil(n-1-v.e-(v.V-1))),k1=Math.min(n-1,Math.floor(n-1-v.e));
  let mn=Infinity,mx=-Infinity,sum=0,cnt=0;
  for(let k=k0;k<=k1;k++){const x=raw[k];if(x==null)continue;const t=x/10;if(t<mn)mn=t;if(t>mx)mx=t;sum+=t;cnt++}
  const end=endMs();
  let range;
  if(end){
    const r=a=>{const d=new Date(end-a*60e3+30*60e3);d.setMinutes(0,0,0);return `${WD[d.getDay()]} ${d.getDate()}.${d.getMonth()+1}. ${hm(d)}`};
    range=`${r(n-1-k0)} – ${v.e<30?'jetzt':r(n-1-k1)}`;
  }else range=v.e<30?`letzte ${hrs(k1-k0+1)}`:`vor ${hrs(n-1-k0)} bis ${hrs(n-1-k1)}`;
  const hint=v.extra>0&&v.e<1?' · ◀ wischen':'';
  if(!raw.some(x=>x!=null))stats.textContent='Noch kein Verlauf (1 Wert pro Minute).';
  else if(!cnt)stats.innerHTML=`<span>Keine Werte in diesem Abschnitt</span><span>${range}${hint}</span>`;
  else stats.innerHTML=`<span>Min ${fmt(mn)}</span><span>Max ${fmt(mx)}</span><span>Ø ${fmt(sum/cnt)}</span><span>${range}${hint}</span>`;
  drawChart(el.querySelector('canvas'),raw,css(COLORS[i]),v,k0,k1);
}

function drawChart(cv,raw,color,v,k0,k1){
  const dpr=window.devicePixelRatio||1,W=v.W,H=CH,n=raw.length;
  if(cv.width!==Math.round(W*dpr)||cv.height!==Math.round(H*dpr)){cv.style.width=W+'px';cv.width=Math.round(W*dpr);cv.height=Math.round(H*dpr)}
  const g=cv.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,W,H);
  const L=CL,T=CT,pw=W-CL-CR,ph=H-CT-CB;
  // Temperature scale from the whole history so it does not jump while swiping.
  let lo=Infinity,hi=-Infinity;
  for(const x of raw)if(x!=null){if(x<lo)lo=x;if(x>hi)hi=x}
  if(lo>hi){lo=0;hi=100}
  lo/=10;hi/=10;
  if(hi-lo<4){const c=(hi+lo)/2;lo=c-2;hi=c+2}
  const span=hi-lo,step=span>40?10:span>16?5:span>8?2:1;
  lo=Math.floor(lo/step)*step;hi=Math.ceil(hi/step)*step;
  const y=t=>T+ph-(t-lo)/(hi-lo)*ph;
  const xa=a=>L+pw-(a-v.e)*v.p;            // a = minutes before the newest value
  const x=k=>xa(n-1-k);
  g.font='11px system-ui,sans-serif';g.fillStyle=css('--mut');g.strokeStyle=css('--line');g.lineWidth=1;
  g.textAlign='right';g.textBaseline='middle';
  for(let t=lo;t<=hi+1e-9;t+=step){g.beginPath();g.moveTo(L,y(t));g.lineTo(L+pw,y(t));g.stroke();g.fillText(t,L-5,y(t));}
  // Time grid: every 6 h for one day per screen, 12 h for up to 2 days, else daily.
  const S=v.V<=DAY?6:v.V<=2*DAY?12:24;
  const ticks=[],end=endMs(),aL=v.e+v.V-1,aR=v.e;
  if(end){
    const tL=end-aL*60e3,d=new Date(tL);d.setMinutes(0,0,0);
    if(d.getTime()<tL)d.setHours(d.getHours()+1);
    while(d.getHours()%S)d.setHours(d.getHours()+1);
    for(;d.getTime()<=end-aR*60e3+1;d.setHours(d.getHours()+S)){
      const a=(end-d.getTime())/60e3;
      // Midnight: weekday and date, only the weekday if the days are too narrow for that.
      const day=S*60*v.p<60?WD[d.getDay()]:`${WD[d.getDay()]} ${d.getDate()}.${d.getMonth()+1}.`;
      ticks.push([a,d.getHours()==0?day:hm(d)]);
    }
  }else{
    for(let a=Math.ceil(aR/(S*60))*S*60;a<=aL;a+=S*60)ticks.push([a,a?'-'+a/60+' h':'jetzt']);
  }
  g.textBaseline='top';g.textAlign='center';
  ticks.forEach(([a,lab])=>{
    const px=xa(a),w=g.measureText(lab).width;
    g.beginPath();g.moveTo(px,T);g.lineTo(px,T+ph);g.stroke();
    g.fillText(lab,Math.max(L+w/2,Math.min(L+pw-w/2,px)),T+ph+5);
  });
  g.save();g.beginPath();g.rect(L,0,pw,H);g.clip();
  if(v.p<0.15){drawBand(g,raw,color,x,y,ka0(k0),kb0(k1,n),L,pw,v.p);g.restore();return}
  g.strokeStyle=color;g.lineWidth=2;g.lineJoin='round';g.beginPath();
  let pen=false;
  const lone=[],ka=ka0(k0),kb=kb0(k1,n);
  for(let k=ka;k<=kb;k++){
    const val=raw[k];if(val==null){pen=false;continue}
    const px=x(k),py=y(val/10);
    if(pen)g.lineTo(px,py);else{g.moveTo(px,py);if(raw[k+1]==null&&(k==0||raw[k-1]==null))lone.push([px,py]);}
    pen=true;
  }
  g.stroke();
  g.fillStyle=color;lone.forEach(([px,py])=>{g.beginPath();g.arc(px,py,2,0,2*Math.PI);g.fill();});
  g.restore();
}

function ka0(k0){return Math.max(0,k0-1)}
function kb0(k1,n){return Math.min(n-1,k1+1)}
// Many values per pixel ('Alles' over several days): per pixel column a band from the
// lowest to the highest value, with the average as a line.
function drawBand(g,raw,color,x,y,ka,kb,L,pw,p){
  const C=Math.ceil(pw)+1,mn=new Float32Array(C).fill(Infinity),mx=new Float32Array(C).fill(-Infinity),sm=new Float32Array(C),ct=new Uint16Array(C);
  for(let k=ka;k<=kb;k++){
    const val=raw[k];if(val==null)continue;
    const c=Math.floor(x(k)-L);if(c<0||c>=C)continue;
    const t=val/10;if(t<mn[c])mn[c]=t;if(t>mx[c])mx[c]=t;sm[c]+=t;ct[c]++;
  }
  // contiguous runs of columns with data
  const runs=[];let a=-1;
  for(let c=0;c<=C;c++){if(c<C&&ct[c]){if(a<0)a=c}else if(a>=0){runs.push([a,c-1]);a=-1}}
  // Smooth over about 2 h (at least one compressor cycle): a pixel column covers only part
  // of a cycle, so without this the band edges and the average ripple.
  const w=Math.ceil(60*p),lo=new Float32Array(C),hi=new Float32Array(C),av=new Float32Array(C);
  runs.forEach(([a,b])=>{
    for(let c=a;c<=b;c++){
      let l=Infinity,h=-Infinity,su=0,n=0;
      for(let j=Math.max(a,c-w);j<=Math.min(b,c+w);j++){if(mn[j]<l)l=mn[j];if(mx[j]>h)h=mx[j];su+=sm[j];n+=ct[j]}
      lo[c]=l;hi[c]=h;av[c]=su/n;
    }
  });
  g.fillStyle=color;g.globalAlpha=0.3;
  runs.forEach(([a,b])=>{
    g.beginPath();
    for(let c=a;c<=b;c++)g.lineTo(L+c+0.5,y(hi[c])-1);
    for(let c=b;c>=a;c--)g.lineTo(L+c+0.5,y(lo[c])+1);
    g.closePath();g.fill();
  });
  g.globalAlpha=1;g.strokeStyle=color;g.lineWidth=1.5;g.lineJoin='round';
  runs.forEach(([a,b])=>{
    g.beginPath();
    for(let c=a;c<=b;c++)g.lineTo(L+c+0.5,y(av[c]));
    if(a==b)g.lineTo(L+a+1.5,y(av[a]));
    g.stroke();
  });
}

// Whole history as CSV for Excel (semicolons, decimal comma, UTF-8 with BOM).
function downloadCsv(){
  if(!hist||!state){alert('Noch kein Verlauf geladen.');return}
  const idx=state.sensors.map((s,i)=>i).filter(i=>state.sensors[i].configured&&hist.data[i]);
  if(!idx.length){alert('Kein Fühler eingerichtet.');return}
  const n=hist.data[idx[0]].length,end=endMs();
  const q=t=>'"'+String(t).replace(/"/g,'""')+'"';
  const lines=[[end?'Zeit':'Minuten vor jetzt',...idx.map(i=>state.sensors[i].name||'Fühler '+(i+1))].map(q).join(';')];
  for(let k=0;k<n;k++){
    let t;
    if(end){const d=new Date(end-(n-1-k)*60e3);t=`${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())} ${hm(d)}`}
    else t=-(n-1-k);
    lines.push([t,...idx.map(i=>{const x=hist.data[i][k];return x==null?'':(x/10).toFixed(1).replace('.',',')})].join(';'));
  }
  const now=end?new Date(end):new Date();
  const name=`kuehlschrank-${now.getFullYear()}-${pad(now.getMonth()+1)}-${pad(now.getDate())}.csv`;
  const url=URL.createObjectURL(new Blob(['﻿'+lines.join('\r\n')+'\r\n'],{type:'text/csv;charset=utf-8'}));
  const a=document.createElement('a');a.href=url;a.download=name;document.body.appendChild(a);a.click();a.remove();
  setTimeout(()=>URL.revokeObjectURL(url),10000);
}

function loadState(){return fetch('/api/state').then(r=>r.json()).then(s=>{state=s;render()}).catch(()=>{})}
// Whole history once, then only the new values (the ESP sends everything again if its
// history changed in between, e.g. after a restart or swapping probes).
// Requests run one after another: a second request sent before the first reply arrived
// would carry the same "since" and its reply would no longer fit onto the page's copy.
let histQueue=Promise.resolve();
function loadHist(){return histQueue=histQueue.then(loadHistNow)}
function loadHistNow(){
  const q=hist?`?since=${hist.seq}&id=${hist.id}`:'';
  return fetch('/api/history'+q).then(r=>r.json()).then(h=>{
    if(hist&&h.id===hist.id&&h.from===hist.seq){
      h.data.forEach((d,i)=>{const a=hist.data[i];a.push(...d);if(a.length>h.len)a.splice(0,a.length-h.len)});
      Object.assign(hist,{seq:h.seq,age:h.age,epoch:h.epoch,len:h.len});
    }else hist=h;
    render();
  }).catch(()=>{})
}
function refreshAll(){return loadState().then(loadHist)}
function fillNames(){if(!state)return;$('name0').value=state.sensors[0].name;$('name1').value=state.sensors[1].name}
function fail(id){return ()=>{$(id).textContent='Keine Verbindung zum Gerät. Bitte erneut versuchen.'}}

function saveSettings(){
  post('/api/settings',{name0:$('name0').value,name1:$('name1').value,bright:$('bright').value})
    .then(j=>{$('setmsg').textContent=j.ok?'Gespeichert.':(j.error||'Fehler beim Speichern.');refreshAll()})
    .catch(fail('setmsg'));
}
function saveAp(){
  const pw=$('appass').value;
  if(pw&&(pw.length<8||pw.length>63)){$('apmsg').textContent='Das Passwort muss 8 bis 63 Zeichen lang sein.';return}
  const data={apauto:$('apauto').checked?'1':'0'};if(pw)data.appass=pw;
  post('/api/settings',data).then(j=>{
    if(!j.ok){$('apmsg').textContent=j.error||'Fehler beim Speichern.';return}
    $('appass').value='';
    $('apmsg').textContent=pw?'Gespeichert. Der Hotspot startet neu: Handy mit dem neuen Passwort wieder verbinden.':'Gespeichert.';
    setTimeout(loadState,2000);
  }).catch(()=>{$('apmsg').textContent=pw?'Gespeichert. Handy mit dem neuen Passwort wieder verbinden.':'Fehler beim Speichern.'});
}
function factoryReset(){
  if(!confirm('Wirklich alles auf Werkseinstellungen zurücksetzen?'))return;
  post('/api/reset').catch(()=>{});
  $('resetmsg').textContent='Wird zurückgesetzt. Danach mit WLAN „Kuehlschrank“ (Passwort „kuehlschrank“) verbinden.';
}
function forget(i){
  if(!confirm('Fühler '+(i+1)+' entfernen? Der Verlauf dieses Fühlers wird gelöscht.'))return;
  post('/api/forget',{slot:i}).then(()=>refreshAll()).then(fillNames).catch(fail('setmsg'));
}
function swap(){post('/api/swap').then(()=>refreshAll()).then(fillNames).catch(fail('setmsg'))}
function saveWifi(){
  const joining=!!$('ssid').value;
  post('/api/wifi',{ssid:$('ssid').value,pass:$('pass').value}).then(j=>{
    if(!j.ok){$('wifimsg').textContent=j.error||'Fehler beim Speichern.';return}
    $('wifimsg').textContent=joining?'Gespeichert, verbinde … Dabei wechselt der Hotspot den Funkkanal: Das Handy verliert kurz die Verbindung und muss sich eventuell neu verbinden. Die Anzeige zeigt danach die neue Adresse (IP), erreichbar auch unter http://kuehlschrank.local/':'Heim-WLAN getrennt.';
    setTimeout(loadState,8000);
  }).catch(fail('wifimsg'));
}
function scan(){
  $('wifimsg').textContent='Suche Netze …';
  let tries=0;
  const poll=()=>fetch('/api/scan').then(r=>r.json()).then(j=>{
    if(j.busy){if(++tries<20)setTimeout(poll,1500);else $('wifimsg').textContent='Suche dauert zu lange. Bitte später erneut versuchen.';return}
    const sel=$('nets');sel.innerHTML='<option value="">– Netz auswählen –</option>';
    j.nets.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)';sel.appendChild(o)});
    $('wifimsg').textContent=j.nets.length+' Netz(e) gefunden.';
  }).catch(fail('wifimsg'));
  poll();
}
function upload(){
  const f=$('fw').files[0];if(!f){$('fwmsg').textContent='Bitte zuerst eine Datei wählen.';return}
  const fd=new FormData();fd.append('firmware',f);
  const xhr=new XMLHttpRequest();
  xhr.upload.onprogress=e=>{if(e.lengthComputable)$('fwmsg').textContent='Hochladen … '+Math.round(e.loaded/e.total*100)+' %'};
  xhr.onload=()=>{$('fwmsg').textContent=xhr.status==200?'Fertig. Das Gerät startet neu, Seite in 15 s neu laden.':'Fehler: '+xhr.responseText};
  xhr.onerror=()=>{$('fwmsg').textContent='Upload fehlgeschlagen.'};
  xhr.open('POST','/update');xhr.setRequestHeader('X-FM','1');xhr.send(fd);
}

// Send the phone's clock so the history shows real times even without internet.
post('/api/time',{t:Math.floor(Date.now()/1000)}).catch(()=>{});
refreshAll();
setInterval(loadState,5000);
setInterval(loadHist,60000);
window.addEventListener('resize',render);
</script>
</body>
</html>
)rawliteral";
