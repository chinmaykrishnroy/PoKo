#pragma once
#include <pgmspace.h>

const char poko_web_html[] PROGMEM = R"POKOHTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Poko Control</title>
<style>
:root{--bg:#0f1115;--surface:#181b20;--surface2:#22262c;--text:#edf0f3;--muted:#9da6b0;--line:#30353d;--accent:#00c8ff;--green:#3aba7d;--red:#ef6a73;--warn:#f18450}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,sans-serif;padding:16px;max-width:800px;margin:0 auto}
header{display:flex;align-items:center;justify-content:space-between;padding-bottom:16px;border-bottom:1px solid var(--line);margin-bottom:20px}
.brand{display:flex;align-items:center;gap:12px}
.logo{width:36px;height:36px;border-radius:8px;background:var(--accent);display:grid;place-items:center;color:#000;font-weight:900;font-size:20px}
.title strong{font-size:18px;display:block}
.title small{color:var(--muted);font-size:12px}
.online{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--muted)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--green)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px;margin-bottom:20px}
.card{background:var(--surface);border:1px solid var(--line);border-radius:10px;padding:16px}
.card h3{font-size:14px;color:var(--muted);margin-bottom:10px;text-transform:uppercase;letter-spacing:0.5px}
.stat-val{font-size:22px;font-weight:700;color:var(--text)}
.stat-sub{font-size:11px;color:var(--muted);margin-top:4px}
.control-row{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:12px}
.control-row label{font-size:13px;color:var(--muted)}
input[type=range]{flex:1;accent-color:var(--accent)}
.btn{background:var(--surface2);border:1px solid var(--line);color:var(--text);padding:9px 15px;border-radius:7px;cursor:pointer;font-weight:600;font-size:13px;text-decoration:none;display:inline-flex;align-items:center;justify-content:center;gap:6px;transition:0.15s}
.btn:hover{background:var(--line);border-color:var(--muted)}
.btn.accent{background:var(--accent);color:#000;border:none}
.btn.danger{background:transparent;border-color:var(--red);color:var(--red)}
.btn.danger:hover{background:var(--red);color:#fff}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:12px}
table{width:100%;border-collapse:collapse;font-size:13px}
td{padding:8px 0;border-bottom:1px solid var(--line)}
td:last-child{text-align:right;font-weight:600}
input[type=text],input[type=password]{width:100%;padding:8px 10px;border-radius:6px;background:var(--bg);border:1px solid var(--line);color:var(--text);margin-top:4px}
.form-group{margin-bottom:12px}
.form-group label{font-size:12px;color:var(--muted)}
</style>
</head>
<body>
<header>
  <div class="brand">
    <div class="logo">P</div>
    <div class="title">
      <strong>Poko</strong>
      <small>ESP32-S3-LCD-0.85</small>
    </div>
  </div>
  <div class="online">
    <span class="dot"></span>
    <span id="uptime">Loading...</span>
  </div>
</header>

<div class="grid">
  <div class="card">
    <h3>System Status</h3>
    <table>
      <tr><td>WiFi IP</td><td id="ip">--</td></tr>
      <tr><td>SSID</td><td id="ssid">--</td></tr>
      <tr><td>RSSI</td><td id="rssi">--</td></tr>
      <tr><td>Free Heap</td><td id="heap">--</td></tr>
      <tr><td>Free PSRAM</td><td id="psram">--</td></tr>
      <tr><td>CPU</td><td id="cpu">--</td></tr>
    </table>
  </div>

  <div class="card">
    <h3>Display & Audio</h3>
    <div class="control-row">
      <label>Brightness</label>
      <input type="range" id="brSlider" min="5" max="100" value="80" oninput="setBrightness(this.value)">
      <span id="brVal" style="font-size:12px;width:35px;text-align:right">80%</span>
    </div>
    <div class="control-row">
      <label>Volume</label>
      <input type="range" id="volSlider" min="0" max="100" value="75" oninput="setVolume(this.value)">
      <span id="volVal" style="font-size:12px;width:35px;text-align:right">75%</span>
    </div>
    <h3 style="margin-top:16px">RGB LED Ring</h3>
    <div class="control-row">
      <label>Custom Color</label>
      <input type="color" id="ledColorPicker" value="#00c8ff" oninput="pickLEDColor(this.value)" style="width:50px;height:32px;padding:2px;border:1px solid var(--line);border-radius:6px;cursor:pointer;background:var(--surface2)">
    </div>
    <div class="actions" style="margin-top:8px">
      <button class="btn" onclick="setLED(255,0,0)">Red</button>
      <button class="btn" onclick="setLED(0,255,0)">Green</button>
      <button class="btn" onclick="setLED(0,180,255)">Cyan</button>
      <button class="btn" onclick="setLED(255,200,0)">Amber</button>
      <button class="btn" onclick="setLED(0,0,0)">Off</button>
    </div>
  </div>

  <div class="card">
    <h3>Apps & Controls</h3>
    <div class="actions">
      <button class="btn accent" onclick="setApp(0)">Launcher</button>
      <button class="btn accent" onclick="setApp(1)">Info App</button>
    </div>
    <h3 style="margin-top:18px">System Actions</h3>
    <div class="actions">
      <a class="btn" href="/ota">OTA Update</a>
      <button class="btn" onclick="resetDrivers()">Reset Drivers</button>
      <button class="btn danger" onclick="rebootDevice()">Reboot</button>
    </div>
  </div>

  <div class="card">
    <h3>WiFi Settings</h3>
    <form onsubmit="saveWiFi(event)">
      <div class="form-group">
        <label>SSID</label>
        <input type="text" id="wifi_ssid" required placeholder="Enter WiFi Name">
      </div>
      <div class="form-group">
        <label>Password</label>
        <input type="password" id="wifi_pass" value="12345678">
      </div>
      <button type="submit" class="btn accent" style="width:100%">Save & Reconnect</button>
    </form>
  </div>
</div>

<script>
async function api(url){try{const r=await fetch(url);return await r.json();}catch(e){return null;}}
async function refresh(){
  const d=await api('/api/health');
  if(!d) return;
  document.getElementById('ip').textContent=d.ip||'--';
  document.getElementById('ssid').textContent=d.ssid||'--';
  document.getElementById('rssi').textContent=d.rssi?(d.rssi+' dBm'):'--';
  document.getElementById('heap').textContent=d.heap_free?(Math.round(d.heap_free/1024)+' KB'):'--';
  document.getElementById('psram').textContent=d.psram_free?(Math.round(d.psram_free/1024)+' KB'):'--';
  document.getElementById('cpu').textContent=d.cpu_mhz?(d.cpu_mhz+' MHz'):'--';
  const sec=Math.floor((d.uptime_ms||0)/1000);
  const m=Math.floor(sec/60), s=sec%60;
  document.getElementById('uptime').textContent=`Up ${m}m ${s}s`;
}
let brTimer=null;
function setBrightness(v){
  document.getElementById('brVal').textContent=v+'%';
  clearTimeout(brTimer);
  brTimer=setTimeout(()=>fetch('/api/sys?brightness='+v),100);
}
let volTimer=null;
function setVolume(v){
  document.getElementById('volVal').textContent=v+'%';
  clearTimeout(volTimer);
  volTimer=setTimeout(()=>fetch('/api/sys?volume='+v),100);
}
function setLED(r,g,b){fetch(`/api/led?r=${r}&g=${g}&b=${b}`);}
function pickLEDColor(hex){
  const r=parseInt(hex.substr(1,2),16);
  const g=parseInt(hex.substr(3,2),16);
  const b=parseInt(hex.substr(5,2),16);
  setLED(r,g,b);
}
function setApp(s){fetch('/api/app?state='+s);}
function resetDrivers(){fetch('/api/reset');alert('Drivers reset');}
function rebootDevice(){if(confirm('Reboot Poko?')) fetch('/api/reboot');}
async function saveWiFi(e){
  e.preventDefault();
  const s=document.getElementById('wifi_ssid').value;
  const p=document.getElementById('wifi_pass').value;
  try {
    await fetch(`/api/wifi?ssid=${encodeURIComponent(s)}&pass=${encodeURIComponent(p)}`,{method:'POST'});
  } catch(err){}
  alert('WiFi saved! Poko is rebooting to connect to ' + s + '...');
  setTimeout(()=>location.reload(), 4000);
}
refresh();
setInterval(refresh, 3000);
</script>
</body>
</html>
)POKOHTML";
