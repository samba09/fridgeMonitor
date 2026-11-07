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
<h1>Kühlschrank</h1>
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
        '<div class="stats mut"></div><canvas></canvas>';
      box.appendChild(el);
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

function renderHistory(i,el){
  const cv=el.querySelector('canvas'),stats=el.querySelector('.stats');
  if(!hist||!hist.data[i]){stats.textContent='';return;}
  const raw=hist.data[i],vals=raw.filter(v=>v!=null).map(v=>v/10);
  if(vals.length){
    const mn=Math.min(...vals),mx=Math.max(...vals),avg=vals.reduce((a,b)=>a+b,0)/vals.length;
    stats.innerHTML=`<span>Min ${fmt(mn)}</span><span>Max ${fmt(mx)}</span><span>Ø ${fmt(avg)}</span><span>${Math.round(raw.length/60*10)/10} h Verlauf</span>`;
  }else stats.textContent='Noch kein Verlauf (1 Wert pro Minute).';
  drawChart(cv,raw,css(COLORS[i]));
}

function drawChart(cv,raw,color){
  const dpr=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.clientHeight;
  cv.width=W*dpr;cv.height=H*dpr;
  const g=cv.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,W,H);
  const L=34,R=6,T=6,B=20,pw=W-L-R,ph=H-T-B,N=1440;
  const vals=raw.filter(v=>v!=null).map(v=>v/10);
  let lo=vals.length?Math.min(...vals):0,hi=vals.length?Math.max(...vals):10;
  if(hi-lo<4){const c=(hi+lo)/2;lo=c-2;hi=c+2}
  const span=hi-lo,step=span>40?10:span>16?5:span>8?2:1;
  lo=Math.floor(lo/step)*step;hi=Math.ceil(hi/step)*step;
  const y=v=>T+ph-(v-lo)/(hi-lo)*ph;
  const x=k=>L+pw-(raw.length-1-k)/(N-1)*pw;
  g.font='11px system-ui,sans-serif';g.fillStyle=css('--mut');g.strokeStyle=css('--line');g.lineWidth=1;
  g.textAlign='right';g.textBaseline='middle';
  for(let v=lo;v<=hi+1e-9;v+=step){g.beginPath();g.moveTo(L,y(v));g.lineTo(L+pw,y(v));g.stroke();g.fillText(v,L-5,y(v));}
  g.textAlign='center';g.textBaseline='top';
  const endMs=hist.epoch?(hist.epoch-hist.age)*1000:0;
  for(let hAgo=24;hAgo>=0;hAgo-=6){
    const px=L+pw-hAgo/24*pw;
    g.beginPath();g.moveTo(px,T);g.lineTo(px,T+ph);g.stroke();
    let lab=hAgo?'-'+hAgo+' h':'jetzt';
    if(endMs){const d=new Date(endMs-hAgo*3600e3);lab=d.toLocaleTimeString('de-DE',{hour:'2-digit',minute:'2-digit'});}
    g.textAlign=hAgo==24?'left':hAgo==0?'right':'center';
    g.fillText(lab,px,T+ph+5);
  }
  g.strokeStyle=color;g.lineWidth=2;g.lineJoin='round';g.beginPath();
  let pen=false;
  const lone=[];
  raw.forEach((v,k)=>{if(v==null){pen=false;return}const px=x(k),py=y(v/10);
    if(pen)g.lineTo(px,py);else{g.moveTo(px,py);if(raw[k+1]==null)lone.push([px,py]);}pen=true;});
  g.stroke();
  g.fillStyle=color;lone.forEach(([px,py])=>{g.beginPath();g.arc(px,py,2,0,2*Math.PI);g.fill();});
}

function loadState(){return fetch('/api/state').then(r=>r.json()).then(s=>{state=s;render()}).catch(()=>{})}
function loadHist(){return fetch('/api/history').then(r=>r.json()).then(h=>{hist=h;render()}).catch(()=>{})}
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
