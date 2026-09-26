#pragma once
#include <pgmspace.h>

const char poko_web_html[] PROGMEM = R"POKOHTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PoKo Control</title>
<style>
:root{--bg:#0f1115;--surface:#181b20;--surface2:#22262c;--text:#edf0f3;--muted:#9da6b0;--line:#30353d;--accent:#00c8ff;--green:#3aba7d;--red:#ef6a73;--warn:#f18450}
body.theme-light{--bg:#f4f5f8;--surface:#ffffff;--surface2:#e6e9ef;--text:#11151a;--muted:#667085;--line:#d0d5dd;--accent:#0066cc}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,sans-serif;padding:16px;max-width:880px;margin:0 auto;transition:background 0.2s,color 0.2s}
header{display:flex;align-items:center;justify-content:space-between;padding-bottom:14px;border-bottom:1px solid var(--line);margin-bottom:20px}
.brand{display:flex;align-items:center;gap:12px}
.logo{width:36px;height:36px;border-radius:8px;background:var(--accent);display:grid;place-items:center;color:#000;font-weight:900;font-size:20px}
.title strong{font-size:18px;display:block}
.title small{color:var(--muted);font-size:12px}
.header-right{display:flex;align-items:center;gap:12px}
.online{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--muted)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--green)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:14px;margin-bottom:20px}
.card{background:var(--surface);border:1px solid var(--line);border-radius:10px;padding:16px;box-shadow:0 1px 3px rgba(0,0,0,0.1)}
.card h3{font-size:13px;color:var(--muted);margin-bottom:12px;text-transform:uppercase;letter-spacing:0.5px}
.control-row{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:10px}
.control-row label{font-size:13px;color:var(--muted);min-width:90px}
input[type=range]{flex:1;accent-color:var(--accent)}
.btn{background:var(--surface2);border:1px solid var(--line);color:var(--text);padding:8px 14px;border-radius:7px;cursor:pointer;font-weight:600;font-size:12px;text-decoration:none;display:inline-flex;align-items:center;justify-content:center;gap:6px;transition:0.15s}
.btn:hover{background:var(--line);border-color:var(--muted)}
.btn.accent{background:var(--accent);color:#fff;border:none}
.btn.danger{background:transparent;border-color:var(--red);color:var(--red)}
.btn.danger:hover{background:var(--red);color:#fff}
.actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
table{width:100%;border-collapse:collapse;font-size:13px}
td{padding:7px 0;border-bottom:1px solid var(--line)}
td:last-child{text-align:right;font-weight:600}
input[type=text],input[type=password],input[type=number],select{width:100%;padding:8px 10px;border-radius:6px;background:var(--bg);border:1px solid var(--line);color:var(--text);margin-top:4px}
.form-group{margin-bottom:10px}
.form-group label{font-size:12px;color:var(--muted)}
</style>
</head>
<body>
<header>
  <div class="brand">
    <div class="logo">P</div>
    <div class="title">
      <strong>PoKo</strong>
      <small>ESP32-S3-LCD-0.85</small>
    </div>
  </div>
  <div class="header-right">
    <div class="online">
      <span class="dot"></span>
      <span id="uptime">Connecting...</span>
    </div>
    <button class="btn" id="themeBtn" onclick="toggleTheme()">Toggle Theme</button>
  </div>
</header>

<div class="grid">
  <!-- System Status -->
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

  <!-- Audio & Display -->
  <div class="card">
    <h3>Display & Audio Controls</h3>
    <div class="control-row">
      <label>Brightness</label>
      <input type="range" id="brSlider" min="5" max="100" value="80" oninput="setBrightness(this.value)">
      <span id="brVal" style="font-size:12px;width:38px;text-align:right">80%</span>
    </div>
    <div class="control-row">
      <label>Master Vol</label>
      <input type="range" id="masterVolSlider" min="10" max="100" value="100" oninput="setMasterVolume(this.value)">
      <span id="masterVolVal" style="font-size:12px;width:38px;text-align:right">100%</span>
    </div>
    <div class="control-row">
      <label>App Volume</label>
      <input type="range" id="volSlider" min="0" max="100" value="75" oninput="setVolume(this.value)">
      <span id="volVal" style="font-size:12px;width:38px;text-align:right">75%</span>
    </div>

    <h3 style="margin-top:16px">RGB LED Ring</h3>
    <div class="control-row">
      <label>Custom Color</label>
      <input type="color" id="ledColorPicker" value="#00c8ff" oninput="pickLEDColor(this.value)" style="width:50px;height:30px;padding:2px;border:1px solid var(--line);border-radius:6px;cursor:pointer;background:var(--surface2)">
    </div>
    <div class="actions" style="margin-top:8px">
      <button class="btn" onclick="setLED(255,0,0)">Red</button>
      <button class="btn" onclick="setLED(0,255,0)">Green</button>
      <button class="btn" onclick="setLED(0,180,255)">Cyan</button>
      <button class="btn" onclick="setLED(255,200,0)">Amber</button>
      <button class="btn" onclick="setLED(0,0,0)">Off</button>
    </div>
  </div>

  <!-- SSync / Snapclient -->
  <div class="card">
    <h3>SSync (Snapclient)</h3>
    <form onsubmit="saveSnap(event)">
      <div class="form-group">
        <label>Server Host</label>
        <input type="text" id="snap_host" value="192.168.0.20" required>
      </div>
      <div class="form-group">
        <label>Server Port (1780 Web / 1704 Stream)</label>
        <input type="number" id="snap_port" value="1780" required>
      </div>
      <button type="submit" class="btn accent" style="width:100%">Update Snapcast Server</button>
    </form>
    <div class="control-row" style="margin-top:12px">
      <label>SSync Vol</label>
      <input type="range" id="snapVolSlider" min="0" max="100" value="75" oninput="setSnapVolume(this.value)">
      <button class="btn" id="muteBtn" onclick="toggleSnapMute()">Mute</button>
    </div>
    <table style="margin-top:10px">
      <tr><td>Client Name</td><td>PoKo</td></tr>
      <tr><td>Status</td><td id="snapStatus">--</td></tr>
      <tr><td>Codec</td><td id="snapCodec">--</td></tr>
    </table>
  </div>

  <!-- Gallery & Slideshow -->
  <div class="card">
    <h3>Gallery & Slideshow</h3>
    <div class="form-group">
      <label>Slideshow Timer</label>
      <select id="slideTimerSel" onchange="setSlideTimer(this.value)">
        <option value="0">Off (Manual)</option>
        <option value="3">3 seconds</option>
        <option value="5">5 seconds</option>
        <option value="10">10 seconds</option>
        <option value="15">15 seconds</option>
        <option value="30">30 seconds</option>
        <option value="60">60 seconds</option>
      </select>
    </div>
    <div class="form-group" style="margin-top:10px">
      <label>128x128 Photo Cropper (Preview)</label>
      <input type="file" id="photoInput" accept="image/*" onchange="previewCrop(event)" style="font-size:12px">
      <div style="margin-top:8px;text-align:center">
        <canvas id="cropCanvas" width="128" height="128" style="border:1px solid var(--line);border-radius:4px;background:#000"></canvas>
      </div>
    </div>
  </div>

  <!-- Apps Navigation -->
  <div class="card">
    <h3>Apps Navigation</h3>
    <div class="actions">
      <button class="btn accent" onclick="setApp(0)">Launcher</button>
      <button class="btn" onclick="setApp(1)">System Info</button>
      <button class="btn" onclick="setApp(2)">Clock</button>
      <button class="btn" onclick="setApp(3)">SSync (Snap)</button>
      <button class="btn" onclick="setApp(4)">Music</button>
      <button class="btn" onclick="setApp(5)">Video</button>
      <button class="btn" onclick="setApp(6)">Gallery</button>
      <button class="btn" onclick="setApp(7)">Settings</button>
    </div>
    <h3 style="margin-top:16px">System Maintenance</h3>
    <div class="actions">
      <a class="btn" href="/ota">OTA Update</a>
      <button class="btn" onclick="resetDrivers()">Reset Drivers</button>
      <button class="btn danger" onclick="rebootDevice()">Reboot Device</button>
    </div>
  </div>

  <!-- WiFi Configuration -->
  <div class="card">
    <h3>WiFi Settings</h3>
    <form onsubmit="saveWiFi(event)">
      <div class="form-group">
        <label>SSID</label>
        <input type="text" id="wifi_ssid" required placeholder="Enter WiFi SSID">
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
let curTheme = 'dark';
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
  
  if(d.theme && d.theme!==curTheme){
    curTheme=d.theme;
    applyThemeUI(curTheme);
  }
  if(d.master_vol!=null) {
    document.getElementById('masterVolSlider').value=d.master_vol;
    document.getElementById('masterVolVal').textContent=d.master_vol+'%';
  }
  if(d.gallery_timer!=null) {
    document.getElementById('slideTimerSel').value=d.gallery_timer;
  }
  if(d.snap_host) document.getElementById('snap_host').value=d.snap_host;
  if(d.snap_port) document.getElementById('snap_port').value=d.snap_port;

  const sec=Math.floor((d.uptime_ms||0)/1000);
  const m=Math.floor(sec/60), s=sec%60;
  document.getElementById('uptime').textContent=`Up ${m}m ${s}s`;

  // Update snap status
  const snap=await api('/api/snap');
  if(snap) {
    document.getElementById('snapStatus').textContent = snap.connected ? (snap.playing ? 'PLAYING' : 'CONNECTED') : 'OFFLINE';
    document.getElementById('snapCodec').textContent = snap.codec || '--';
    if(snap.volume!=null) document.getElementById('snapVolSlider').value = snap.volume;
    document.getElementById('muteBtn').textContent = snap.muted ? 'Unmute' : 'Mute';
  }
}

function applyThemeUI(theme){
  if(theme==='light'){
    document.body.classList.add('theme-light');
    document.getElementById('themeBtn').textContent='Theme: Light';
  } else {
    document.body.classList.remove('theme-light');
    document.getElementById('themeBtn').textContent='Theme: Dark';
  }
}

async function toggleTheme(){
  curTheme = (curTheme==='dark') ? 'light' : 'dark';
  applyThemeUI(curTheme);
  await fetch('/api/theme?mode='+curTheme);
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

let masterTimer=null;
function setMasterVolume(v){
  document.getElementById('masterVolVal').textContent=v+'%';
  clearTimeout(masterTimer);
  masterTimer=setTimeout(()=>fetch('/api/sys?master_vol='+v),100);
}

function setSnapVolume(v){
  fetch('/api/snap?vol='+v);
}

function toggleSnapMute(){
  fetch('/api/snap?mute=1');
}

async function saveSnap(e){
  e.preventDefault();
  const h=document.getElementById('snap_host').value;
  const p=document.getElementById('snap_port').value;
  await fetch(`/api/snap?host=${encodeURIComponent(h)}&port=${encodeURIComponent(p)}`);
  alert('Snapcast server updated to ' + h + ':' + p);
}

function setSlideTimer(v){
  fetch('/api/gallery?timer='+v);
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
function rebootDevice(){if(confirm('Reboot PoKo?')) fetch('/api/reboot');}

async function saveWiFi(e){
  e.preventDefault();
  const s=document.getElementById('wifi_ssid').value;
  const p=document.getElementById('wifi_pass').value;
  try {
    await fetch(`/api/wifi?ssid=${encodeURIComponent(s)}&pass=${encodeURIComponent(p)}`,{method:'POST'});
  } catch(err){}
  alert('WiFi saved! PoKo is rebooting to connect to ' + s + '...');
  setTimeout(()=>location.reload(), 4000);
}

function previewCrop(e){
  const file = e.target.files[0];
  if(!file) return;
  const img = new Image();
  img.onload = function(){
    const canvas = document.getElementById('cropCanvas');
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0,0,128,128);
    const size = Math.min(img.width, img.height);
    const sx = (img.width - size)/2;
    const sy = (img.height - size)/2;
    ctx.drawImage(img, sx, sy, size, size, 0, 0, 128, 128);
  };
  img.src = URL.createObjectURL(file);
}

refresh();
setInterval(refresh, 3000);
</script>
</body>
</html>
)POKOHTML";
