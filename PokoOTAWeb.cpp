#include "PokoOTAWeb.h"

const char poko_ota_html[] PROGMEM = R"OTAHTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Poko OTA</title>
<style>
body{background:#0f1115;color:#edf0f3;font-family:system-ui,sans-serif;padding:24px;max-width:500px;margin:40px auto;text-align:center}
h2{margin-bottom:12px;color:#00c8ff}
p{color:#9da6b0;font-size:14px;margin-bottom:24px}
input[type=file]{background:#181b20;border:1px solid #30353d;padding:12px;border-radius:8px;width:100%;margin-bottom:16px;color:#edf0f3}
button{background:#00c8ff;color:#000;border:none;padding:12px 24px;border-radius:8px;font-weight:700;font-size:15px;cursor:pointer;width:100%}
.progress{background:#181b20;height:12px;border-radius:6px;overflow:hidden;margin-top:20px;display:none;border:1px solid #30353d}
.bar{height:100%;background:#00c8ff;width:0%}
</style>
</head>
<body>
<h2>Poko Firmware Update</h2>
<p>Upload compiled .bin to update device wirelessly.</p>
<form id="f" onsubmit="upload(event)">
  <input type="file" id="bin" accept=".bin" required>
  <button type="submit" id="btn">Flash Firmware</button>
  <div class="progress" id="prog"><div class="bar" id="bar"></div></div>
</form>
<script>
function upload(e){
  e.preventDefault();
  const file = document.getElementById('bin').files[0];
  if(!file) return;
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/ota/upload', true);
  document.getElementById('prog').style.display='block';
  document.getElementById('btn').disabled=true;
  xhr.upload.onprogress = ev => {
    if(ev.lengthComputable){
      const p = Math.round((ev.loaded / ev.total)*100);
      document.getElementById('bar').style.width = p + '%';
      document.getElementById('btn').textContent = 'Flashing ' + p + '%';
    }
  };
  xhr.onload = () => {
    if(xhr.status === 200){
      alert('Update Successful! Rebooting...');
      setTimeout(()=>location.href='/', 3000);
    } else {
      alert('Update Failed: ' + xhr.responseText);
      document.getElementById('btn').disabled=false;
      document.getElementById('btn').textContent = 'Flash Firmware';
    }
  };
  const formData = new FormData();
  formData.append('update', file);
  xhr.send(formData);
}
</script>
</body>
</html>)OTAHTML";
