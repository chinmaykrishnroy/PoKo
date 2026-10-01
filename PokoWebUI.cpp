#include "PokoWebUI.h"

const char poko_web_html[] PROGMEM = R"POKOHTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0f1115">
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Crect width='64' height='64' rx='12' fill='%2300c8ff'/%3E%3Ctext x='50%25' y='54%25' dominant-baseline='middle' text-anchor='middle' font-family='Arial Black,sans-serif' font-weight='900' font-size='36' fill='%23000'%3EP%3C/text%3E%3C/svg%3E">
<title>PoKo Control</title>
<style>
:root{color-scheme:dark;--bg:#0f1115;--surface:#181b20;--surface2:#22262c;--text:#edf0f3;--muted:#9da6b0;--line:#30353d;--black:#f5f7f9;--green:#3aba7d;--blue:#00c8ff;--red:#ef6a73;--yellow:#e4b94d;--orange:#f18450;--focus:#00c8ff;--shadow:0 10px 30px rgba(0,0,0,.25)}
[data-theme=light]{color-scheme:light;--bg:#f4f6f8;--surface:#fff;--surface2:#eef1f4;--text:#15191f;--muted:#68717d;--line:#dce1e6;--black:#111418;--green:#168b5b;--blue:#0055aa;--red:#cf3e48;--yellow:#c48909;--orange:#d85d20;--focus:#0055aa;--shadow:0 8px 24px rgba(17,20,24,.08)}
*{box-sizing:border-box}html,body{margin:0;min-height:100%;background:var(--bg);color:var(--text);font-family:Inter,ui-sans-serif,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}button,input,select,textarea{font:inherit}button{color:inherit}svg{width:18px;height:18px;display:inline-block;vertical-align:middle;flex-shrink:0}.upload-zone svg{width:42px;height:42px;margin:0 auto 8px;display:block}.empty svg{width:32px;height:32px}
.shell{min-height:100vh;display:grid;grid-template-columns:220px minmax(0,1fr)}
.sidebar{position:sticky;top:0;height:100vh;padding:16px 12px;border-right:1px solid var(--line);background:var(--surface);display:flex;flex-direction:column;z-index:20}
.brand{height:48px;display:flex;align-items:center;gap:10px;padding:0 8px}.brand-mark{width:32px;height:32px;border-radius:8px;background:var(--blue);color:#000;display:grid;place-items:center;font-weight:900;font-size:18px}.brand strong{display:block;font-size:15px;font-weight:800}.brand small{display:block;color:var(--muted);font-size:11px;margin-top:1px}
.nav{display:grid;gap:2px;margin-top:18px}.nav button{height:40px;padding:0 10px;border-radius:6px;display:flex;align-items:center;gap:10px;color:var(--muted);cursor:pointer;text-align:left;border:0;background:transparent;font-size:13px}.nav button:hover{background:var(--surface2);color:var(--text)}.nav button.active{background:var(--text);color:var(--surface)}.nav svg{width:17px;height:17px;flex-shrink:0}
.side-foot{margin-top:auto;padding:10px 8px 4px}.online{display:flex;align-items:center;gap:8px;font-size:12px;color:var(--muted)}.dot{width:8px;height:8px;border-radius:50%;background:var(--green);box-shadow:0 0 0 3px color-mix(in srgb,var(--green) 18%,transparent)}.dot.off{background:var(--red);box-shadow:none}
.workspace{min-width:0}.topbar{height:62px;position:sticky;top:0;z-index:15;background:color-mix(in srgb,var(--bg) 85%,transparent);backdrop-filter:blur(14px);border-bottom:1px solid var(--line);display:flex;align-items:center;justify-content:space-between;padding:0 clamp(16px,3vw,40px)}.topbar h1{font-size:17px;margin:0;font-weight:700}
.top-actions{display:flex;gap:7px}.icon-btn,.btn{border:1px solid var(--line);background:var(--surface);height:35px;border-radius:6px;display:inline-flex;align-items:center;justify-content:center;gap:7px;cursor:pointer;transition:.15s}.icon-btn{width:35px;padding:0}.icon-btn:hover,.btn:hover{border-color:var(--muted)}.icon-btn svg,.btn svg{width:15px;height:15px}.btn{padding:0 12px;font-weight:650;font-size:13px}.btn.primary{background:var(--text);color:var(--surface);border-color:var(--text)}.btn.blue{background:var(--blue);color:#000;border-color:var(--blue);font-weight:700}.btn.danger{color:var(--red);border-color:color-mix(in srgb,var(--red) 35%,var(--line))}.btn.danger:hover{background:var(--red);color:#fff;border-color:var(--red)}.btn:disabled{opacity:.45;cursor:not-allowed}
.content{padding:26px clamp(16px,3vw,40px) 52px;max-width:1400px;margin:0 auto}.view{display:none}.view.active{display:block}
.section-head{display:flex;justify-content:space-between;align-items:flex-start;gap:16px;margin-bottom:18px}.section-head h2{font-size:21px;margin:0 0 4px;font-weight:700}.section-head p{margin:0;color:var(--muted);font-size:13px}.actions{display:flex;gap:8px;flex-wrap:wrap}
.metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));border:1px solid var(--line);background:var(--surface);border-radius:8px;overflow:hidden;margin-bottom:18px}.metric{padding:16px 18px;border-right:1px solid var(--line)}.metric:last-child{border-right:0}.metric small{color:var(--muted);display:block;font-size:10px;text-transform:uppercase;font-weight:700;letter-spacing:.5px}.metric strong{display:block;font-size:22px;margin-top:7px;font-weight:800}.metric span{display:block;color:var(--muted);font-size:11px;margin-top:4px}
.columns{display:grid;grid-template-columns:minmax(0,1.3fr) minmax(280px,.7fr);gap:16px}.panel{border:1px solid var(--line);background:var(--surface);border-radius:8px;overflow:hidden}.panel-head{min-height:48px;padding:0 15px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid var(--line)}.panel-head h3{font-size:13px;margin:0;font-weight:700}.panel-body{padding:15px}.status-list{display:grid}.status-row{min-height:43px;display:grid;grid-template-columns:140px minmax(0,1fr);align-items:center;border-bottom:1px solid var(--line);font-size:13px}.status-row:last-child{border-bottom:0}.status-row span:first-child{color:var(--muted)}.status-row strong{text-align:right;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.badge{display:inline-flex;align-items:center;min-height:22px;padding:0 8px;border-radius:999px;background:var(--surface2);font-size:11px;font-weight:700}.badge.good{color:var(--green)}.badge.bad{color:var(--red)}.badge.info{color:var(--blue)}
.app-grid{display:grid;grid-template-columns:repeat(4,minmax(140px,1fr));gap:12px}.app-tile{position:relative;min-height:128px;padding:14px;border:1px solid var(--line);background:var(--surface);border-radius:8px;text-align:left;cursor:pointer;overflow:hidden;transition:border-color .15s,box-shadow .15s}.app-tile:hover{border-color:var(--text);box-shadow:var(--shadow)}.app-tile.active{border-color:var(--green);background:color-mix(in srgb,var(--green) 6%,var(--surface));box-shadow:inset 3px 0 var(--green)}.app-icon{width:36px;height:36px;border-radius:7px;display:grid;place-items:center;color:#000;font-size:0}.app-icon svg{width:19px;height:19px}.app-tile strong{display:block;font-size:13px;margin-top:14px;font-weight:700}.app-tile small{display:block;color:var(--muted);font-size:11px;margin-top:3px}.app-tile .arr{position:absolute;right:12px;top:12px;color:var(--muted)}.app-tile .arr svg{width:15px;height:15px}.app-tile.active .arr{color:var(--green)}
.form-section{border:1px solid var(--line);background:var(--surface);border-radius:8px;margin-bottom:14px}.form-title{padding:13px 15px;border-bottom:1px solid var(--line)}.form-title h3{margin:0;font-size:13px;font-weight:700}.form-title p{margin:3px 0 0;color:var(--muted);font-size:11px}.form-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:0 20px;padding:4px 15px 12px}.field{min-height:64px;padding:10px 0;border-bottom:1px solid var(--line);display:grid;grid-template-columns:minmax(0,1fr) minmax(110px,180px);gap:12px;align-items:center}.field:nth-last-child(-n+2){border-bottom:0}.field label,.field .label{font-size:13px;font-weight:650}.field small{display:block;color:var(--muted);font-size:10px;font-weight:400;margin-top:3px;line-height:1.35}.field input[type=text],.field input[type=password],.field input[type=number],.field select{height:33px;width:100%;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 8px;outline:none}.range-wrap{display:grid;grid-template-columns:1fr 44px;gap:7px;align-items:center}.range-wrap input[type=range]{width:100%;accent-color:var(--blue)}.range-value{text-align:right;font-size:12px;color:var(--muted)}.switch{position:relative;width:40px;height:22px;justify-self:end}.switch input{position:absolute;opacity:0}.switch span{position:absolute;inset:0;border-radius:12px;background:var(--line);cursor:pointer}.switch span:after{content:"";position:absolute;width:16px;height:16px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.18s}.switch input:checked+span{background:var(--green)}.switch input:checked+span:after{transform:translateX(18px)}.form-foot{padding:11px 15px;border-top:1px solid var(--line);display:flex;justify-content:flex-end;gap:8px}
input:focus,select:focus,textarea:focus{border-color:var(--focus)!important;box-shadow:0 0 0 3px color-mix(in srgb,var(--focus) 16%,transparent)}
.storage-meter{height:6px;background:var(--surface2);border-radius:4px;overflow:hidden;margin-top:8px}.storage-meter span{display:block;height:100%;background:var(--blue)}
.file-toolbar{display:flex;align-items:center;gap:8px;margin-bottom:12px}.search{height:34px;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 10px;outline:none;flex:1;min-width:0}
.gallery-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:10px}.gallery-item{border:1px solid var(--line);border-radius:7px;overflow:hidden;position:relative;background:var(--surface2)}.gallery-item img{display:block;width:100%;aspect-ratio:1;object-fit:cover}.gallery-item .gi-foot{padding:6px 8px;display:flex;align-items:center;justify-content:space-between}.gi-name{font-size:11px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1;min-width:0}.gi-del{width:24px;height:24px;border:0;background:transparent;color:var(--red);cursor:pointer;display:grid;place-items:center;border-radius:4px;flex-shrink:0}.gi-del:hover{background:color-mix(in srgb,var(--red) 12%,transparent)}.gi-del svg{width:13px;height:13px}
.upload-zone{border:2px dashed var(--line);border-radius:8px;padding:24px;text-align:center;cursor:pointer;transition:.15s}.upload-zone:hover,.upload-zone.drag{border-color:var(--blue);background:color-mix(in srgb,var(--blue) 6%,var(--surface))}.upload-zone p{margin:6px 0 0;font-size:12px;color:var(--muted)}
.progress{height:6px;background:var(--surface2);border-radius:4px;overflow:hidden;margin-top:10px}.progress span{height:100%;display:block;background:var(--blue);width:0;transition:width .1s}
.crop-wrap{display:grid;grid-template-columns:auto 1fr;gap:14px;align-items:start;margin-top:14px}
.modal-backdrop{position:fixed;inset:0;background:rgba(0,0,0,.52);z-index:100;display:none;align-items:center;justify-content:center;padding:16px}.modal-backdrop.open{display:flex}.modal{width:min(680px,100%);max-height:min(88vh,780px);display:flex;flex-direction:column;background:var(--surface);border:1px solid var(--line);border-radius:9px;box-shadow:0 22px 70px rgba(0,0,0,.3)}.modal-head,.modal-foot{padding:11px 14px;display:flex;align-items:center;justify-content:space-between;gap:10px}.modal-head{border-bottom:1px solid var(--line)}.modal-head h3{margin:0;font-size:14px;font-weight:700}.modal-body{padding:15px;overflow:auto}.modal-foot{border-top:1px solid var(--line);justify-content:flex-end}
.toast-stack{position:fixed;right:16px;bottom:16px;z-index:150;display:grid;gap:7px}.toast{min-width:240px;max-width:360px;padding:11px 13px;border-radius:7px;background:var(--text);color:var(--surface);box-shadow:var(--shadow);font-size:12px;font-weight:600;animation:fadeIn .2s}.toast.error{background:var(--red);color:#fff}@keyframes fadeIn{from{opacity:0;transform:translateY(6px)}to{opacity:1;transform:none}}
.mobile-nav{display:none}
@media(max-width:1080px){.metrics{grid-template-columns:repeat(2,1fr)}.metric:nth-child(2){border-right:0}.metric:nth-child(-n+2){border-bottom:1px solid var(--line)}.app-grid{grid-template-columns:repeat(3,1fr)}.columns{grid-template-columns:1fr}.form-grid{grid-template-columns:1fr}.field:nth-last-child(-n+2){border-bottom:1px solid var(--line)}.field:last-child{border-bottom:0}}
@media(max-width:700px){.shell{display:block}.sidebar{display:none}.workspace{padding-bottom:66px}.topbar{height:54px;padding:0 14px}.content{padding:18px 14px 32px}.mobile-nav{position:fixed;display:grid;grid-template-columns:repeat(6,1fr);left:0;right:0;bottom:0;z-index:50;height:60px;padding-bottom:env(safe-area-inset-bottom);background:var(--surface);border-top:1px solid var(--line)}.mobile-nav button{display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px;color:var(--muted);font-size:9px;border:0;background:transparent;cursor:pointer}.mobile-nav button.active{color:var(--text)}.mobile-nav svg{width:18px;height:18px}.metrics{grid-template-columns:1fr 1fr}.metric{padding:13px 14px}.metric strong{font-size:18px}.app-grid{grid-template-columns:repeat(2,minmax(0,1fr));gap:9px}.section-head{display:block}.section-head .actions{margin-top:10px}.crop-wrap{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="shell">
  <aside class="sidebar">
    <div class="brand">
      <div class="brand-mark">P</div>
      <div><strong>PoKo</strong><small>Device control</small></div>
    </div>
    <nav class="nav" id="desktopNav"></nav>
    <div class="side-foot">
      <div class="online"><span class="dot" id="sideDot"></span><span id="sideStatus">Connecting...</span></div>
    </div>
  </aside>
  <div class="workspace">
    <header class="topbar">
      <h1 id="pageTitle">Overview</h1>
      <div class="top-actions">
        <button class="icon-btn" title="Refresh" onclick="refreshView()" id="refreshBtn"></button>
        <button class="icon-btn" title="Toggle theme" onclick="toggleTheme()" id="themeBtn"></button>
        <a class="btn" href="/ota" id="otaTop"></a>
      </div>
    </header>
    <main class="content">
      <section class="view active" id="view-dashboard"></section>
      <section class="view" id="view-apps"></section>
      <section class="view" id="view-controls"></section>
      <section class="view" id="view-gallery"></section>
      <section class="view" id="view-settings"></section>
      <section class="view" id="view-system"></section>
    </main>
  </div>
</div>
<nav class="mobile-nav" id="mobileNav"></nav>
<div class="modal-backdrop" id="modalBackdrop" onclick="backdropClose(event)">
  <div class="modal">
    <div class="modal-head"><h3 id="modalTitle"></h3><button class="icon-btn" onclick="closeModal()" id="modalClose"></button></div>
    <div class="modal-body" id="modalBody"></div>
    <div class="modal-foot" id="modalFoot"></div>
  </div>
</div>
<div class="toast-stack" id="toasts"></div>
<input hidden type="file" id="galleryPicker" accept="image/*">
<script>
const $=id=>document.getElementById(id);
let activeView='dashboard',healthCache=null,croppedBlob=null,galleryImage=null,galleryImageUrl=null,galleryPreviewVersion=0,curTheme='dark';

const paths={
  dashboard:'<path d="M3 3h7v7H3zM14 3h7v7h-7zM3 14h7v7H3zM14 14h7v7h-7z"/>',
  apps:'<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>',
  controls:'<path d="M12 5v14M5 12h14"/><circle cx="12" cy="12" r="9"/>',
  gallery:'<rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><path d="m21 15-5-5L5 21"/>',
  settings:'<path d="M12 15.5a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7z"/><path d="M19.4 15a1.7 1.7 0 0 0 .34 1.88l.06.06-2.83 2.83-.06-.06A1.7 1.7 0 0 0 15 19.4a1.7 1.7 0 0 0-1 .6 1.7 1.7 0 0 0-.4 1v.1h-4v-.1a1.7 1.7 0 0 0-1.1-1.6 1.7 1.7 0 0 0-1.88.34l-.06.06-2.83-2.83.06-.06A1.7 1.7 0 0 0 4.6 15a1.7 1.7 0 0 0-.6-1 1.7 1.7 0 0 0-1-.4h-.1v-4H3a1.7 1.7 0 0 0 1.6-1.1 1.7 1.7 0 0 0-.34-1.88l-.06-.06 2.83-2.83.06.06A1.7 1.7 0 0 0 9 4.6a1.7 1.7 0 0 0 1-.6 1.7 1.7 0 0 0 .4-1v-.1h4V3a1.7 1.7 0 0 0 1.1 1.6 1.7 1.7 0 0 0 1.88-.34l.06-.06 2.83 2.83-.06.06A1.7 1.7 0 0 0 19.4 9c.15.36.36.7.6 1 .28.3.64.42 1 .4h.1v4H21a1.7 1.7 0 0 0-1.6.6z"/>',
  refresh:'<path d="M20 6v5h-5M4 18v-5h5"/><path d="M18.5 9A7 7 0 0 0 6 6.5L4 9m2 6.5A7 7 0 0 0 18 18l2-3"/>',
  sun:'<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.93 4.93l1.42 1.42M17.65 17.65l1.42 1.42M2 12h2M20 12h2M4.93 19.07l1.42-1.42M17.65 6.35l1.42-1.42"/>',
  moon:'<path d="M20 15.5A8.5 8.5 0 0 1 8.5 4 8.5 8.5 0 1 0 20 15.5z"/>',
  upload:'<path d="M12 16V4M7 9l5-5 5 5M4 20h16"/>',
  trash:'<path d="M4 7h16M9 7V4h6v3M7 7l1 13h8l1-13"/>',
  x:'<path d="m6 6 12 12M18 6 6 18"/>',
  save:'<path d="M4 3h14l2 2v16H4zM8 3v6h8V3M8 21v-7h8v7"/>',
  arrow:'<path d="M5 12h14M14 7l5 5-5 5"/>',
  info:'<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7h.01"/>',
  music:'<path d="M9 18V5l10-2v13M9 9l10-2"/><circle cx="6" cy="18" r="3"/><circle cx="16" cy="16" r="3"/>',
  video:'<rect x="3" y="5" width="14" height="14" rx="2"/><path d="m17 10 4-3v10l-4-3z"/>',
  clock:'<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  wifi:'<path d="M5 12.5a10 10 0 0 1 14 0M8.5 16a5 5 0 0 1 7 0M12 20h.01"/>',
  home:'<path d="m3 11 9-8 9 8v10h-6v-6H9v6H3z"/>',
  brightness:'<circle cx="12" cy="12" r="5"/><path d="M12 2v2M12 20v2M4.93 4.93l1.42 1.42M17.65 17.65l1.42 1.42M2 12h2M20 12h2M4.93 19.07l1.42-1.42M17.65 6.35l1.42-1.42"/>',
  volume:'<path d="M11 5 6 9H2v6h4l5 4zM19.07 4.93a10 10 0 0 1 0 14.14M15.54 8.46a5 5 0 0 1 0 7.07"/>',
  snapcast:'<circle cx="12" cy="12" r="2"/><path d="M7 7a7 7 0 0 0 0 10M17 7a7 7 0 0 1 0 10M4 4a11 11 0 0 0 0 16M20 4a11 11 0 0 1 0 16"/>',
  reboot:'<path d="M12 2v10M6.3 5.3a8 8 0 1 0 11.4 0"/>',
  eye:'<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
  plus:'<path d="M12 5v14M5 12h14"/>',
  image:'<rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><path d="m21 15-5-5L5 21"/>',
  gallery:'<rect x="3" y="4" width="18" height="16" rx="2"/><path d="m3 17 5-5 4 4 3-3 6 5"/><circle cx="8" cy="9" r="1"/>',
  sparkle:'<path d="m12 2 2 7 7 3-7 2-2 8-2-8-7-2 7-3zM4 2v3M2.5 3.5h3M20 19v3M18.5 20.5h3"/>',
  system:'<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M7 8h10M7 12h10M7 16h6"/>',
  battery:'<rect x="2" y="7" width="18" height="10" rx="2"/><path d="M22 10v4M5 10h10"/>'
};
function icon(n){return`<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">${paths[n]||paths.info}</svg>`}

const navItems=[
  ['dashboard','Overview','dashboard'],
  ['apps','Applications','apps'],
  ['controls','Controls','controls'],
  ['gallery','Gallery','gallery'],
  ['settings','Settings','settings'],
  ['system','System','system']
];

function setupChrome(){
  const html=navItems.map(([id,label,ic])=>`<button data-view="${id}" onclick="navigate('${id}')">${icon(ic)}<span>${label}</span></button>`).join('');
  $('desktopNav').innerHTML=html;
  $('mobileNav').innerHTML=navItems.map(([id,label,ic])=>`<button data-view="${id}" onclick="navigate('${id}')">${icon(ic)}<span>${label}</span></button>`).join('');
  $('refreshBtn').innerHTML=icon('refresh');
  $('otaTop').innerHTML=icon('upload')+'OTA';
  $('modalClose').innerHTML=icon('x');
  updateThemeBtn();
}

function navigate(view){
  activeView=view;
  document.querySelectorAll('.view').forEach(x=>x.classList.toggle('active',x.id==='view-'+view));
  document.querySelectorAll('[data-view]').forEach(x=>x.classList.toggle('active',x.dataset.view===view));
  $('pageTitle').textContent=navItems.find(x=>x[0]===view)[1];
  renderView();
  history.replaceState(null,'','#'+view);
}

function setTheme(t){
  curTheme=t;
  document.documentElement.dataset.theme=t;
  localStorage.setItem('poko-theme',t);
  document.querySelector('meta[name=theme-color]').content=t==='dark'?'#0f1115':'#f4f6f8';
  updateThemeBtn();
}
async function applyTheme(mode){try{await api('/api/theme?mode='+mode);setTheme(mode)}catch(e){toast(e.message,true)}}
function toggleTheme(){applyTheme(curTheme==='dark'?'light':'dark');}
function updateThemeBtn(){if($('themeBtn'))$('themeBtn').innerHTML=icon(curTheme==='dark'?'sun':'moon');}

async function api(path,opt={}){const r=await fetch(path,opt);const ct=r.headers.get('content-type')||'';const body=ct.includes('json')?await r.json():await r.text();if(!r.ok||body?.ok===false)throw new Error(body.error||body||`HTTP ${r.status}`);return body;}
function esc(v){return String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function fmtBytes(n){n=Number(n)||0;if(n<1024)return n+' B';if(n<1048576)return(n/1024).toFixed(1)+' KB';return(n/1048576).toFixed(2)+' MB'}
function fmtUptime(ms){const s=Math.floor((ms||0)/1000);const h=Math.floor(s/3600),m=Math.floor(s%3600/60),ss=s%60;return(h?h+'h ':'')+(m?m+'m ':'')+(ss+'s');}

function toast(msg,error=false){const el=document.createElement('div');el.className='toast'+(error?' error':'');el.textContent=msg;$('toasts').append(el);setTimeout(()=>el.remove(),3800)}
function setOnline(ok){$('sideDot').classList.toggle('off',!ok);$('sideStatus').textContent=ok?'Device online':'Unavailable'}
function liveText(id,val){const el=$(id);if(el&&el.textContent!==String(val??''))el.textContent=String(val??'');}

function showModal(title,body,foot=''){$('modalTitle').textContent=title;$('modalBody').innerHTML=body;$('modalFoot').innerHTML=foot;$('modalBackdrop').classList.add('open')}
function closeModal(){$('modalBackdrop').classList.remove('open')}
function backdropClose(e){if(e.target===$('modalBackdrop'))closeModal()}

// ── Dashboard ─────────────────────────────────────────────────────────────────
async function renderDashboard(){
  const root=$('view-dashboard');
  root.innerHTML=`
    <div class="section-head"><div><h2>PoKo at a glance</h2><p>Live device health and quick controls.</p></div><span class="badge" id="dashOnline">Connecting</span></div>
    <div class="metrics">
      <div class="metric"><small>Active App</small><strong id="dApp">...</strong><span id="dShell"></span></div>
      <div class="metric"><small>Wi-Fi</small><strong id="dRssi">...</strong><span id="dSsid"></span></div>
      <div class="metric"><small>Free PSRAM</small><strong id="dPsram">...</strong><span id="dHeap"></span></div>
      <div class="metric"><small>Battery estimate</small><strong id="dBattery">...</strong><span id="dBatVolt"></span></div>
    </div>
    <div class="columns">
      <div class="panel">
        <div class="panel-head"><h3>Quick launch</h3><button class="btn" onclick="navigate('apps')">${icon('arrow')} All apps</button></div>
        <div class="panel-body">
          <div class="app-grid" style="grid-template-columns:repeat(4,1fr)">
            ${quickLaunchApps()}
          </div>
        </div>
      </div>
      <div class="panel">
        <div class="panel-head"><h3>Device status</h3><span class="badge" id="dashNet">Connecting</span></div>
        <div class="panel-body status-list">
          <div class="status-row"><span>IP address</span><strong id="dIpRow">...</strong></div>
          <div class="status-row"><span>Uptime</span><strong id="dUptime">...</strong></div>
          <div class="status-row"><span>CPU</span><strong id="dCpu">...</strong></div>
          <div class="status-row"><span>Brightness</span><strong id="dBr">...</strong></div>
          <div class="status-row"><span>Theme</span><strong id="dTheme">...</strong></div>
        </div>
      </div>
    </div>`;
  try{await syncDashboard(await getHealth(true));}catch(e){setOnline(false);liveText('dashOnline','Unavailable');}
}

const APP_NAMES={0:'Launcher',1:'Info',2:'Clock',3:'Video',4:'Music',5:'SSync',6:'Gallery',7:'Pixels',8:'Settings'};
const APP_ICONS={0:'home',1:'info',2:'clock',3:'video',4:'music',5:'snapcast',6:'gallery',7:'sparkle',8:'settings'};
const APP_COLORS={0:'#3aba7d',1:'#3aba7d',2:'#00c8ff',3:'#6d9ff5',4:'#ef6a73',5:'#e4b94d',6:'#f18450',7:'#b05cf5',8:'#9da6b0'};

function quickLaunchApps(){
  return Object.entries(APP_NAMES).slice(0,9).map(([state,name])=>
    `<button class="app-tile" data-state="${state}" onclick="launchApp(${state})">
      <span class="app-icon" style="background:${APP_COLORS[state]||'#666'}">${icon(APP_ICONS[state]||'info')}</span>
      <strong>${esc(name)}</strong>
    </button>`
  ).join('');
}

function syncDashboard(d){
  if(!d)return;
  if((d.theme==='dark'||d.theme==='light')&&d.theme!==curTheme)setTheme(d.theme);
  setOnline(true);
  const appName=APP_NAMES[d.app_state]||('State '+d.app_state);
  liveText('dApp',appName);
  liveText('dShell','');
  liveText('dRssi',d.rssi?(d.rssi+' dBm'):'--');
  liveText('dSsid',d.ssid||'--');
  liveText('dPsram',d.psram_free?Math.round(d.psram_free/1024)+' KB':'--');
  liveText('dHeap',d.heap_free?'Heap '+Math.round(d.heap_free/1024)+' KB':'');
  liveText('dUptime',fmtUptime(d.uptime_ms));
  liveText('dBattery',d.battery_pct>=0?d.battery_pct+'%':'—');
  liveText('dBatVolt',d.battery_pct>=0?Number(d.battery_v).toFixed(2)+' V · estimate':'No battery detected');
  liveText('dIpRow',d.ip||'--');
  liveText('dCpu',d.cpu_mhz?d.cpu_mhz+' MHz':'--');
  liveText('dBr',d.brightness!=null?d.brightness+'%':'--');
  liveText('dTheme',d.theme||'--');
  const ob=$('dashOnline'),net=$('dashNet');
  if(ob){ob.textContent='Online';ob.className='badge good'}
  if(net){net.textContent=d.ssid?'Connected':'Offline';net.className='badge '+(d.ssid?'good':'bad')}
  document.querySelectorAll('[data-state]').forEach(el=>{
    el.classList.toggle('active',String(el.dataset.state)===String(d.app_state));
  });
}

async function getHealth(force=false){
  if(healthCache&&!force)return healthCache;
  try{healthCache=await api('/api/health');return healthCache;}
  catch(e){setOnline(false);throw e;}
}

async function launchApp(state){
  try{
    await api('/api/app?state='+state);
    toast('App launched: '+(APP_NAMES[state]||state));
    healthCache=null;
    setTimeout(()=>{
      getHealth(true).then(d=>{
        syncDashboard(d);
        if(activeView==='apps')syncAppsView(d);
      }).catch(()=>{});
    },400);
  }catch(e){toast(e.message,true);}
}

async function sendKey(k){
  try{
    await api('/api/input?action='+k);
    toast('Button: '+k);
    setTimeout(()=>{getHealth(true).then(d=>{syncDashboard(d);syncAppsView(d);}).catch(()=>{})},300);
  }catch(e){toast(e.message,true);}
}
async function toggleScreen(){
  try{await api('/api/power?screen=toggle');toast('Screen toggled');}
  catch(e){toast(e.message,true);}
}
async function snapAction(act){
  try{
    await api('/api/snap?action='+act);
    toast('SSync: '+act);
    refreshSnapStatus();
  }catch(e){toast(e.message,true);}
}
async function refreshSnapStatus(){
  try{
    const s=await api('/api/snap');
    if(!s)return;
    const txt=s.connected?(s.playing?'PLAYING':(s.suspended?'SUSPENDED':'CONNECTED')):'OFFLINE';
    liveText('appsSnapStatus',txt);
    liveText('appsSnapCodec',s.codec||'opus');
    liveText('appsSnapLatency',(s.latency_ms!=null?s.latency_ms:'--')+' ms');
    liveText('appsSnapBuf',(s.buffer_ms!=null?s.buffer_ms:'--')+' ms');
    const elVol=$('appsSnapVol');if(elVol&&!elVol.matches(':active')&&s.volume!=null)elVol.value=s.volume;
    const mb=$('appsSnapMuteBtn');if(mb)mb.textContent=s.muted?'Unmute':'Mute';
  }catch(e){}
}

const APP_DESC={
  0:'Launcher carousel & app chooser',
  1:'System hardware, battery & metrics',
  2:'IST clock & style switcher',
  3:'MPEG1 video playback stream',
  4:'TCP audio streaming player',
  5:'Snapcast multi-room audio sync',
  6:'Photo frame & slideshow',
  7:'WS2812 8-LED light studio',
  8:'Device settings & power profiles'
};

// ── Applications ──────────────────────────────────────────────────────────────
function renderApps(){
  const root=$('view-apps');
  const d=healthCache||{};
  const curState=d.app_state!=null?d.app_state:0;
  const curName=APP_NAMES[curState]||'Launcher';
  const curDesc=APP_DESC[curState]||'';
  const curColor=APP_COLORS[curState]||'#3aba7d';
  const curIcon=APP_ICONS[curState]||'home';

  root.innerHTML=`
    <div class="section-head">
      <div><h2>Applications</h2><p>Device application state, remote navigation and dedicated app controls.</p></div>
      <button class="btn primary" onclick="launchApp(0)">${icon('home')} Home</button>
    </div>

    <!-- Active App Banner & Remote Keypad -->
    <div class="form-section" style="margin-bottom:16px;border-color:var(--focus);box-shadow:0 2px 8px rgba(0,0,0,0.12)">
      <div class="form-title" style="display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px">
        <div style="display:flex;align-items:center;gap:12px">
          <span class="app-icon" id="heroAppIcon" style="width:36px;height:36px;border-radius:8px;background:${curColor};display:grid;place-items:center">${icon(curIcon)}</span>
          <div>
            <h3 style="margin:0;font-size:14px">Active App: <strong id="heroAppName">${esc(curName)}</strong></h3>
            <p style="margin:2px 0 0;font-size:11px;color:var(--muted)" id="heroAppDesc">${esc(curDesc)}</p>
          </div>
        </div>
        <span class="badge good" id="heroAppBadge">LIVE ON DEVICE</span>
      </div>
      <div style="padding:10px 15px;background:var(--surface2);display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px">
        <span style="font-size:12px;font-weight:600;color:var(--muted)">Hardware Remote:</span>
        <div style="display:flex;gap:6px;flex-wrap:wrap">
          <button class="btn" onclick="sendKey('prev')">${icon('arrow')} L (Prv)</button>
          <button class="btn" onclick="sendKey('next')">R (Nxt) ${icon('arrow')}</button>
          <button class="btn primary" onclick="sendKey('enter')">2R (Set)</button>
          <button class="btn" onclick="sendKey('back')">2L (Back)</button>
          <button class="btn" onclick="toggleScreen()">Screen</button>
        </div>
      </div>
    </div>

    <!-- App Switcher Grid -->
    <div class="app-grid" style="margin-bottom:20px">
      ${Object.entries(APP_NAMES).map(([state,name])=>`
        <button class="app-tile ${String(state)===String(curState)?'active':''}" data-state="${state}" onclick="launchApp(${state})">
          <span class="arr">${icon('arrow')}</span>
          <span class="app-icon" style="background:${APP_COLORS[state]||'#666'}">${icon(APP_ICONS[state]||'info')}</span>
          <strong>${esc(name)}</strong>
          <small>${APP_DESC[state]||''}</small>
        </button>`).join('')}
    </div>

    <!-- App Specific Controllers -->
    <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px">

      <!-- SSync (Snapcast) Controller -->
      <div class="form-section">
        <div class="form-title" style="display:flex;align-items:center;justify-content:space-between">
          <h3>${icon('snapcast')} SSync (Snapclient)</h3>
          <span class="badge" id="appsSnapStatus">Checking...</span>
        </div>
        <div style="padding:14px 15px">
          <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:12px;font-size:12px">
            <div style="padding:8px 10px;background:var(--surface2);border-radius:6px;border:1px solid var(--line)">
              <div style="color:var(--muted);font-size:11px">Codec</div>
              <strong id="appsSnapCodec">opus</strong>
            </div>
            <div style="padding:8px 10px;background:var(--surface2);border-radius:6px;border:1px solid var(--line)">
              <div style="color:var(--muted);font-size:11px">Latency / Buf</div>
              <strong id="appsSnapLatency">-- ms</strong>
            </div>
          </div>
          <label style="font-size:12px;color:var(--muted)">SSync Volume</label>
          <div class="range-wrap" style="margin-top:6px">
            <input type="range" id="appsSnapVol" min="0" max="100" value="100" oninput="setSnapVolume(this.value)">
            <button class="btn" id="appsSnapMuteBtn" onclick="toggleSnapMute()" style="height:28px;padding:0 8px;font-size:11px">Mute</button>
          </div>
          <div style="display:flex;gap:6px;margin-top:12px">
            <button class="btn primary" style="flex:1" onclick="snapAction('play')">Play / Resume</button>
            <button class="btn" style="flex:1" onclick="snapAction('pause')">Suspend</button>
            <button class="btn" onclick="snapAction('reload')">Reload</button>
          </div>
        </div>
      </div>

      <!-- Media Player Controller -->
      <div class="form-section">
        <div class="form-title"><h3>${icon('music')} Media Playback (Music &amp; Video)</h3></div>
        <div style="padding:14px 15px">
          <div style="display:flex;gap:6px;margin-bottom:12px">
            <button class="btn" style="flex:1" onclick="sendKey('prev')">${icon('arrow')} Prv</button>
            <button class="btn primary" style="flex:1" onclick="sendKey('enter')">Play / Pause</button>
            <button class="btn" style="flex:1" onclick="sendKey('next')">Nxt ${icon('arrow')}</button>
            <button class="btn danger" onclick="api('/api/audio/stop').then(()=>toast('Audio stopped'))">Stop</button>
          </div>
          <label style="font-size:12px;color:var(--muted)">App Volume</label>
          <div class="range-wrap" style="margin-top:6px">
            <input type="range" id="appsVolSlider" min="0" max="100" value="${d.app_vol||100}" oninput="setAppVolume(this.value)">
            <span class="range-value" id="appsVolVal">${d.app_vol||100}%</span>
          </div>
          <label style="font-size:12px;color:var(--muted);margin-top:10px;display:block">Master Volume Limit</label>
          <div class="range-wrap" style="margin-top:6px">
            <input type="range" id="appsMasterVolSlider" min="10" max="100" value="${d.master_vol||75}" oninput="setMasterVolume(this.value)">
            <span class="range-value" id="appsMasterVolVal">${d.master_vol||75}%</span>
          </div>
        </div>
      </div>

      <!-- Quick LED Studio -->
      <div class="form-section">
        <div class="form-title"><h3>${icon('sparkle')} NeoPixel 8-LED Ring</h3></div>
        <div style="padding:14px 15px">
          <div style="display:flex;gap:6px;flex-wrap:wrap;margin-bottom:10px">
            <button class="btn" onclick="setPixelMode('spinner')">Chase</button>
            <button class="btn" onclick="setPixelMode('rainbow')">Rainbow</button>
            <button class="btn" onclick="setPixelMode('breathe')">Breathe</button>
            <button class="btn" onclick="setPixelMode('fire')">Fire</button>
            <button class="btn" onclick="setPixelMode('solid')">Solid</button>
            <button class="btn" onclick="setPixelMode('off')">Off</button>
          </div>
          <div style="display:flex;align-items:center;gap:10px">
            <input type="color" id="appsLedColor" value="#${((d.pixel_r||0).toString(16).padStart(2,'0'))+((d.pixel_g||200).toString(16).padStart(2,'0'))+((d.pixel_b||255).toString(16).padStart(2,'0'))}" oninput="pickLEDColor(this.value)" style="width:40px;height:32px;padding:2px;border:1px solid var(--line);border-radius:6px;cursor:pointer;background:var(--bg)">
            <span style="font-size:12px;color:var(--muted)">Click swatch to set ring color</span>
          </div>
        </div>
      </div>

      <!-- Clock App Quick Modes -->
      <div class="form-section">
        <div class="form-title"><h3>${icon('clock')} Clock Styles</h3></div>
        <div style="padding:14px 15px">
          <p style="font-size:12px;color:var(--muted);margin:0 0 10px">Switch mode or style on the clock display:</p>
          <div style="display:flex;gap:6px">
            <button class="btn primary" style="flex:1" onclick="sendKey('prev')">Cycle Mode (L)</button>
            <button class="btn" style="flex:1" onclick="sendKey('next')">Cycle Color (R)</button>
          </div>
        </div>
      </div>

    </div>`;

  refreshSnapStatus();
}

function syncAppsView(d){
  if(!d)return;
  const curState=d.app_state!=null?d.app_state:0;
  const curName=APP_NAMES[curState]||('App '+curState);
  const curDesc=APP_DESC[curState]||'';
  const curColor=APP_COLORS[curState]||'#3aba7d';
  const curIcon=APP_ICONS[curState]||'home';

  liveText('heroAppName',curName);
  liveText('heroAppDesc',curDesc);
  const iconEl=$('heroAppIcon');
  if(iconEl){
    iconEl.style.background=curColor;
    iconEl.innerHTML=icon(curIcon);
  }
  document.querySelectorAll('[data-state]').forEach(el=>{
    el.classList.toggle('active',String(el.dataset.state)===String(curState));
  });

  const volEl=$('appsVolSlider');
  if(volEl&&!volEl.matches(':active')&&d.app_vol!=null){
    volEl.value=d.app_vol;
    liveText('appsVolVal',d.app_vol+'%');
  }
  const mvEl=$('appsMasterVolSlider');
  if(mvEl&&!mvEl.matches(':active')&&d.master_vol!=null){
    mvEl.value=d.master_vol;
    liveText('appsMasterVolVal',d.master_vol+'%');
  }
  refreshSnapStatus();
}

// ── Controls ──────────────────────────────────────────────────────────────────
async function renderControls(){
  let d=healthCache;
  if(!d){try{d=await api('/api/health');healthCache=d;}catch(e){}}
  d=d||{};
  $('view-controls').innerHTML=`
    <div class="section-head"><div><h2>Controls</h2><p>Brightness, volume, RGB LED and network settings.</p></div></div>
    <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px">

      <div class="form-section">
        <div class="form-title"><h3>${icon('brightness')} Display &amp; Audio</h3></div>
        <div style="padding:14px 15px">
          <label style="font-size:12px;color:var(--muted)">Brightness</label>
          <div class="range-wrap" style="margin-top:6px">
            <input type="range" id="brSlider" min="1" max="100" value="${d.brightness||80}" oninput="setBrightness(this.value)">
            <span class="range-value" id="brVal">${d.brightness||80}%</span>
          </div>
          <label style="font-size:12px;color:var(--muted);margin-top:12px;display:block">Master Volume</label>
          <div class="range-wrap" style="margin-top:6px">
            <input type="range" id="masterVolSlider" min="10" max="100" value="${d.master_vol||75}" oninput="setMasterVolume(this.value)">
            <span class="range-value" id="masterVolVal">${d.master_vol||75}%</span>
          </div>
          <label style="font-size:12px;color:var(--muted);margin-top:12px;display:block">App Volume</label>
          <div class="range-wrap" style="margin-top:6px">
            <input type="range" id="volSlider" min="0" max="100" value="${d.app_vol||100}" oninput="setAppVolume(this.value)">
            <span class="range-value" id="volVal">${d.app_vol||100}%</span>
          </div>
          <label style="font-size:12px;color:var(--muted);margin-top:12px;display:block">Amp Boost <span style="font-size:10px;color:var(--muted)">(0–5 dB above unity · use with care)</span></label>
          <div class="range-wrap" style="margin-top:6px">
            <input type="range" id="ampBoostSlider" min="0" max="5" step="1" value="${d.amp_boost!=null?d.amp_boost:0}" oninput="setAmpBoost(this.value)">
            <span class="range-value" id="ampBoostVal">+${d.amp_boost!=null?d.amp_boost:0} dB</span>
          </div>
        </div>
        <div class="form-foot">
          <button class="btn danger" onclick="rebootDevice()">Reboot</button>
          <button class="btn" onclick="fetch('/api/reset').then(()=>toast('Drivers reset'))">Reset Drivers</button>
        </div>
      </div>

      <div class="form-section">
        <div class="form-title"><h3>${icon('sparkle')} NeoPixel 8-LED Ring Studio</h3></div>
        <div style="padding:14px 15px">
          <!-- Live 8-LED Ring & Color Preview Box -->
          <div style="display:flex;align-items:center;justify-content:space-between;gap:12px;background:var(--surface2);padding:10px 14px;border-radius:10px;border:1px solid var(--line);margin-bottom:14px">
            <div style="display:flex;align-items:center;gap:12px">
              <div id="netSwatch" style="width:42px;height:42px;border-radius:8px;background:${'#'+((d.pixel_r||0).toString(16).padStart(2,'0'))+((d.pixel_g||200).toString(16).padStart(2,'0'))+((d.pixel_b||255).toString(16).padStart(2,'0'))};border:2px solid var(--line);box-shadow:0 0 10px rgba(0,0,0,0.3)"></div>
              <div>
                <b id="netHex" style="font-size:13px;display:block">#${((d.pixel_r||0).toString(16).padStart(2,'0'))+((d.pixel_g||200).toString(16).padStart(2,'0'))+((d.pixel_b||255).toString(16).padStart(2,'0'))}</b>
                <span id="netRgb" style="font-size:11px;color:var(--muted)">R:${d.pixel_r||0} G:${d.pixel_g||200} B:${d.pixel_b||255}</span>
              </div>
            </div>
            <div style="display:flex;align-items:center;gap:6px">
              <input type="color" id="ledColorPicker" value="#${((d.pixel_r||0).toString(16).padStart(2,'0'))+((d.pixel_g||200).toString(16).padStart(2,'0'))+((d.pixel_b||255).toString(16).padStart(2,'0'))}" oninput="pickLEDColor(this.value)" style="width:40px;height:32px;padding:2px;border:1px solid var(--line);border-radius:6px;cursor:pointer;background:var(--bg)">
            </div>
          </div>

          <!-- Mode Selector -->
          <label style="font-size:12px;color:var(--muted)">Lighting Mode / Animation Preset</label>
          <select id="pxModeSelect" onchange="setPixelMode(this.value)" style="margin-top:4px;width:100%;height:33px;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 8px">
            <option value="spinner" ${d.pixel_mode===2?'selected':''}>Ring Spinner (Moving Chase)</option>
            <option value="solid" ${d.pixel_mode===1?'selected':''}>Solid Color</option>
            <option value="rainbow" ${d.pixel_mode===3?'selected':''}>Rainbow Spectrum Wave</option>
            <option value="breathe" ${d.pixel_mode===4?'selected':''}>Sine Breathing Pulse</option>
            <option value="fire" ${d.pixel_mode===5?'selected':''}>Fire Flicker</option>
            <option value="off" ${d.pixel_mode===0?'selected':''}>Off (Black)</option>
          </select>

          <!-- RGB Sliders -->
          <div style="margin-top:12px">
            <label style="font-size:12px;color:#ff5555">Red (0-255)</label>
            <div class="range-wrap" style="margin-top:4px">
              <input type="range" id="pxRSlider" min="0" max="255" value="${d.pixel_r||0}" oninput="onRgbChange()">
              <span class="range-value" id="pxRVal">${d.pixel_r||0}</span>
            </div>
            <label style="font-size:12px;color:#55ff55;margin-top:8px;display:block">Green (0-255)</label>
            <div class="range-wrap" style="margin-top:4px">
              <input type="range" id="pxGSlider" min="0" max="255" value="${d.pixel_g||200}" oninput="onRgbChange()">
              <span class="range-value" id="pxGVal">${d.pixel_g||200}</span>
            </div>
            <label style="font-size:12px;color:#5588ff;margin-top:8px;display:block">Blue (0-255)</label>
            <div class="range-wrap" style="margin-top:4px">
              <input type="range" id="pxBSlider" min="0" max="255" value="${d.pixel_b||255}" oninput="onRgbChange()">
              <span class="range-value" id="pxBVal">${d.pixel_b||255}</span>
            </div>
          </div>

          <!-- Target LED Multi-Select (1..8) -->
          <div style="margin-top:14px">
            <div style="display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:6px">
              <label style="font-size:12px;color:var(--muted);font-weight:600">Active LEDs (Click to Toggle)</label>
              <div style="display:flex;gap:5px">
                <button type="button" class="btn ${(d.target_mask===255||d.target_mask==null)?'blue':''}" onclick="setAllLeds(true)" style="font-size:11px;padding:3px 8px;height:24px">All 8</button>
                <button type="button" class="btn" onclick="setAllLeds(false)" style="font-size:11px;padding:3px 8px;height:24px">Clear</button>
              </div>
            </div>
            <div style="display:grid;grid-template-columns:repeat(8,1fr);gap:5px;margin-top:6px">
              ${[0,1,2,3,4,5,6,7].map(i => {
                const active = ((d.target_mask !== undefined ? d.target_mask : 255) & (1 << i)) !== 0;
                return `<button type="button" class="btn ${active ? 'blue' : ''}" onclick="toggleLed(${i})" style="font-size:12px;padding:7px 0;font-weight:700;display:flex;flex-direction:column;align-items:center;gap:2px" title="Toggle LED ${i+1}"><span>${i+1}</span><span style="width:6px;height:6px;border-radius:50%;background:${active ? '#000' : 'var(--line)'}"></span></button>`;
              }).join('')}
            </div>
            <div style="font-size:11px;color:var(--muted);margin-top:5px;text-align:right" id="targetStatusText">Target: ${esc(d.target_label || 'All 8')}</div>
          </div>

          <!-- LED Brightness -->
          <label style="font-size:12px;color:var(--muted);margin-top:12px;display:block">Ring Brightness</label>
          <div class="range-wrap" style="margin-top:4px">
            <input type="range" id="pxBrightSlider" min="1" max="255" value="${d.pixel_bright||40}" oninput="setPixelBrightness(this.value)">
            <span class="range-value" id="pxBrightVal">${Math.round((d.pixel_bright||40)*100/255)}%</span>
          </div>

          <!-- Music Reactive Lighting -->
          <div style="margin-top:14px;padding-top:12px;border-top:1px solid var(--line)">
            <div style="display:flex;align-items:center;justify-content:space-between;gap:12px">
              <div style="min-width:0;flex:1">
                <div style="font-size:13px;font-weight:700;color:var(--accent)">Music Reactive Light</div>
                <div style="font-size:11px;color:var(--muted);margin-top:2px">Pulsates with music playback</div>
              </div>
              <label class="switch" style="flex-shrink:0">
                <input type="checkbox" id="musLightCb" ${d.music_light!==false?'checked':''} onchange="setMusicLight(this.checked)">
                <span></span>
              </label>
            </div>
            <label style="font-size:12px;color:var(--muted);margin-top:10px;display:block">Music Visualizer Effect</label>
            <select id="musFxSelect" onchange="setMusicEffect(this.value)" style="margin-top:4px;width:100%;height:33px;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 8px">
              <option value="auto" ${d.music_effect===0?'selected':''}>Auto (Album Art Dual-Color)</option>
              <option value="progress" ${d.music_effect===1?'selected':''}>Playback Progress (Track Fill + Beat)</option>
              <option value="red" ${d.music_effect===2?'selected':''}>Red Pulse</option>
              <option value="green" ${d.music_effect===3?'selected':''}>Green Glow</option>
              <option value="blue" ${d.music_effect===4?'selected':''}>Blue Ocean</option>
              <option value="cyan" ${d.music_effect===5?'selected':''}>Cyan Neon</option>
              <option value="purple" ${d.music_effect===6?'selected':''}>Purple Haze</option>
              <option value="amber" ${d.music_effect===7?'selected':''}>Amber Flame</option>
              <option value="rainbow" ${d.music_effect===8?'selected':''}>Rainbow Spectrum</option>
            </select>
          </div>

          <!-- SSync Reactive Lighting -->
          <div style="margin-top:14px;padding-top:12px;border-top:1px solid var(--line)">
            <div style="display:flex;align-items:center;justify-content:space-between;gap:12px">
              <div style="min-width:0;flex:1">
                <div style="font-size:13px;font-weight:700;color:var(--accent)">SSync Reactive Light</div>
                <div style="font-size:11px;color:var(--muted);margin-top:2px">Pulsates with Snapcast stream</div>
              </div>
              <label class="switch" style="flex-shrink:0">
                <input type="checkbox" id="ssyLightCb" ${d.ssync_light!==false?'checked':''} onchange="setSSyncLight(this.checked)">
                <span></span>
              </label>
            </div>
            <label style="font-size:12px;color:var(--muted);margin-top:10px;display:block">SSync Effect</label>
            <select id="ssyFxSelect" onchange="setSSyncEffect(this.value)" style="margin-top:4px;width:100%;height:33px;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 8px">
              <option value="vol_hue" ${d.ssync_effect===0?'selected':''}>Volume Adaptive Hue (Green-&gt;Amber-&gt;Red)</option>
              <option value="rainbow" ${d.ssync_effect===1?'selected':''}>Rainbow Wave</option>
              <option value="cyan" ${d.ssync_effect===2?'selected':''}>Cyan Beat Pulse</option>
              <option value="magenta" ${d.ssync_effect===3?'selected':''}>Magenta Beat Pulse</option>
              <option value="amber" ${d.ssync_effect===4?'selected':''}>Amber Glow</option>
            </select>
          </div>

          <!-- Audio Frequency Response Filter -->
          <div style="margin-top:14px;padding-top:12px;border-top:1px solid var(--line)">
            <label style="font-size:12px;color:var(--accent);font-weight:700">Audio Frequency Response</label>
            <label style="font-size:11px;color:var(--muted);margin-top:2px;display:block">Filter audio spectrum for reactive visualizer pulsing</label>
            <select id="freqRespSelect" onchange="setFreqResponse(this.value)" style="margin-top:4px;width:100%;height:33px;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 8px">
              <option value="low" ${d.freq_resp===0?'selected':''}>Low [Bass &amp; Kick &lt; 250 Hz] (Recommended)</option>
              <option value="mid" ${d.freq_resp===1?'selected':''}>Mid [Vocals &amp; Melody 250 Hz - 3 kHz]</option>
              <option value="high" ${d.freq_resp===2?'selected':''}>High [Treble &amp; Cymbals &gt; 3 kHz]</option>
              <option value="all" ${d.freq_resp===3?'selected':''}>All [Full Spectrum Raw Peak]</option>
            </select>
          </div>

          <!-- Quick presets -->
          <div style="margin-top:14px;padding-top:10px;border-top:1px solid var(--line)">
            <label style="font-size:12px;color:var(--muted);margin-bottom:6px;display:block">Quick Colors</label>
            <div class="actions">
              <button class="btn" onclick="applyQuickColor(255,0,0)">Red</button>
              <button class="btn" onclick="applyQuickColor(0,255,0)">Green</button>
              <button class="btn" onclick="applyQuickColor(0,200,255)">Cyan</button>
              <button class="btn" onclick="applyQuickColor(255,140,0)">Amber</button>
              <button class="btn" onclick="applyQuickColor(180,40,255)">Purple</button>
              <button class="btn" onclick="setPixelMode('off')">Off</button>
            </div>
          </div>
        </div>
      </div>

      <div class="form-section">
        <div class="form-title"><h3>${icon('snapcast')} SSync (Snapclient)</h3></div>
        <div style="padding:14px 15px">
          <form onsubmit="saveSnap(event)">
            <label style="font-size:12px;color:var(--muted)">Snapcast Server Host</label>
            <input type="text" id="snap_host" value="${esc(d.snap_host||'')}" placeholder="Snapcast server hostname or IP" style="margin-top:4px;width:100%;height:33px;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 8px" required>
            <label style="font-size:12px;color:var(--muted);margin-top:10px;display:block">Port (1780 Web / 1704 Stream)</label>
            <input type="number" id="snap_port" value="${d.snap_port||1704}" style="margin-top:4px;width:100%;height:33px;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 8px" required>
            <button type="submit" class="btn blue" style="width:100%;margin-top:12px">${icon('save')} Update</button>
          </form>
          <div style="margin-top:12px">
            <label style="font-size:12px;color:var(--muted)">SSync Volume</label>
            <div class="range-wrap" style="margin-top:6px">
              <input type="range" id="snapVolSlider" min="0" max="100" value="75" oninput="setSnapVolume(this.value)">
              <button class="btn" id="muteBtn" onclick="toggleSnapMute()" style="height:28px;padding:0 8px;font-size:11px">Mute</button>
            </div>
          </div>
          <table style="width:100%;border-collapse:collapse;font-size:12px;margin-top:10px">
            <tr style="border-bottom:1px solid var(--line)"><td style="padding:6px 0;color:var(--muted)">Status</td><td style="text-align:right;font-weight:700" id="snapStatus">--</td></tr>
            <tr><td style="padding:6px 0;color:var(--muted)">Codec</td><td style="text-align:right;font-weight:700" id="snapCodec">--</td></tr>
          </table>
        </div>
      </div>

      <div class="form-section">
        <div class="form-title"><h3>${icon('wifi')} Media &amp; Network</h3></div>
        <div style="padding:14px 15px">
          <form onsubmit="saveMediaServer(event)">
            <label style="font-size:12px;color:var(--muted)">Media Server Address</label>
            <input type="text" id="server_addr" placeholder="hostname:port or 192.168.0.15:8765" style="margin-top:4px;width:100%;height:33px;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 8px" required>
            <button type="submit" class="btn blue" style="width:100%;margin-top:10px">${icon('save')} Update Server</button>
          </form>
          <div style="margin-top:18px">
          <form onsubmit="saveWiFi(event)">
            <label style="font-size:12px;color:var(--muted);font-weight:700">Wi-Fi Settings</label>
            <input type="text" id="wifi_ssid" placeholder="SSID" style="margin-top:6px;width:100%;height:33px;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 8px" required>
            <input type="password" id="wifi_pass" placeholder="Password (blank: keep for same SSID)" style="margin-top:6px;width:100%;height:33px;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 8px">
            <button type="submit" class="btn" style="width:100%;margin-top:8px">${icon('wifi')} Save &amp; Reconnect</button>
          </form>
          </div>
        </div>
      </div>

    </div>`;
  // Restore slider state from cache
  if(d.brightness!=null){$('brSlider').value=d.brightness;$('brVal').textContent=d.brightness+'%';}
  if(d.master_vol!=null){$('masterVolSlider').value=d.master_vol;$('masterVolVal').textContent=d.master_vol+'%';}
  if(d.app_vol!=null){$('volSlider').value=d.app_vol;$('volVal').textContent=d.app_vol+'%';}
  if(d.server_addr)$('server_addr').value=d.server_addr;
  // Fetch snap status
  api('/api/snap').then(s=>{
    if(!s)return;
    liveText('snapStatus',s.connected?(s.playing?'PLAYING':'CONNECTED'):'OFFLINE');
    liveText('snapCodec',s.codec||'--');
    if(s.volume!=null)$('snapVolSlider').value=s.volume;
    $('muteBtn').textContent=s.muted?'Unmute':'Mute';
  }).catch(()=>{});
}

// ── Gallery ───────────────────────────────────────────────────────────────────
function renderGallery(){
  const root=$('view-gallery');
  root.innerHTML=`
    <div class="section-head"><div><h2>Gallery</h2><p>Upload, manage and delete 128×128 photos stored in LittleFS.</p></div>
      <div class="actions">
        <select id="slideTimerSel" onchange="setSlideTimer(this.value)" style="height:35px;border:1px solid var(--line);background:var(--surface);color:var(--text);border-radius:6px;padding:0 8px;font-size:13px">
          <option value="0">Slideshow: Off</option>
          <option value="3">3 seconds</option>
          <option value="5">5 seconds</option>
          <option value="10">10 seconds</option>
          <option value="15">15 seconds</option>
          <option value="30">30 seconds</option>
        </select>
        <button class="btn primary" onclick="$('galleryPicker').click()">${icon('upload')} Upload Photo</button>
      </div>
    </div>

    <div class="panel" style="margin-bottom:16px">
      <div class="panel-head"><h3>Upload &amp; Resize (128×128)</h3></div>
      <div class="panel-body">
        <div id="uploadZoneWrap">
          <div class="upload-zone" id="uploadZone" onclick="$('galleryPicker').click()">
            ${icon('image')}
            <p>Select an image, then choose Crop or Fit. It will be resized to a 128×128 JPEG.</p>
          </div>
        </div>
        <div id="cropArea" style="display:none">
          <div class="crop-wrap">
            <canvas id="cropCanvas" width="128" height="128" style="border:1px solid var(--line);border-radius:6px;background:#000;width:128px;height:128px;image-rendering:pixelated;flex-shrink:0"></canvas>
            <div style="min-width:0">
              <div id="cropInfo" style="font-size:12px;color:var(--muted)"></div>
              <label for="imageFitMode" style="font-size:12px;color:var(--muted);display:block;margin-top:10px">Image placement</label>
              <select id="imageFitMode" onchange="renderGalleryPreview()" style="width:100%;height:33px;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 8px">
                <option value="crop">Crop to fill</option>
                <option value="fit">Fit entire image (black bars)</option>
              </select>
              <div style="margin-top:10px">
                <label style="font-size:12px;color:var(--muted)">Photo Name</label>
                <input type="text" id="photoName" placeholder="e.g. sunset" maxlength="24" style="margin-top:4px;width:100%;height:33px;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:6px;padding:0 8px">
              </div>
              <div class="progress" id="uploadProgress" style="display:none"><span id="uploadBar"></span></div>
              <div id="uploadStatus" style="font-size:12px;margin-top:8px"></div>
              <div class="actions" style="margin-top:10px">
                <button class="btn blue" id="uploadBtn" onclick="uploadCroppedPhoto()" disabled>${icon('upload')} Upload to LittleFS</button>
                <button class="btn" onclick="resetCrop()">Cancel</button>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <div class="panel">
      <div class="panel-head">
        <h3>Stored Photos</h3>
        <div style="display:flex;align-items:center;gap:8px">
          <span id="fsInfo" class="badge info">Loading...</span>
          <button class="btn" onclick="loadGalleryFiles()">${icon('refresh')} Refresh</button>
        </div>
      </div>
      <div class="panel-body">
        <div id="galleryGrid" class="gallery-grid"><div style="color:var(--muted);font-size:13px;text-align:center;padding:24px;grid-column:1/-1">Loading...</div></div>
      </div>
    </div>`;

  if(healthCache&&healthCache.gallery_timer!=null){
    const sel=$('slideTimerSel');if(sel)sel.value=String(healthCache.gallery_timer);
  }
  loadGalleryFiles();
}

function resetCrop(){
  galleryPreviewVersion++;
  $('cropArea').style.display='none';
  $('uploadZoneWrap').style.display='';
  croppedBlob=null;
  galleryImage=null;
  if(galleryImageUrl){URL.revokeObjectURL(galleryImageUrl);galleryImageUrl=null;}
  $('galleryPicker').value='';
}

function renderGalleryPreview(){
  if(!galleryImage||!$('cropCanvas'))return;
  const version=++galleryPreviewVersion,canvas=$('cropCanvas'),ctx=canvas.getContext('2d'),img=galleryImage;
  const fit=$('imageFitMode').value==='fit';
  const scale=(fit?Math.min:Math.max)(128/img.width,128/img.height);
  const width=img.width*scale,height=img.height*scale;
  croppedBlob=null;
  $('uploadBtn').disabled=true;
  ctx.imageSmoothingEnabled=true;ctx.imageSmoothingQuality='high';
  ctx.fillStyle='#000';ctx.fillRect(0,0,128,128);
  ctx.drawImage(img,(128-width)/2,(128-height)/2,width,height);
  canvas.toBlob(blob=>{
    if(version!==galleryPreviewVersion||!$('cropInfo'))return;
    if(!blob||blob.size>65536){$('cropInfo').textContent='Could not create a JPEG under 64 KB';return;}
    croppedBlob=blob;
    $('cropInfo').textContent=`128×128 JPEG · ${(blob.size/1024).toFixed(1)} KB · ${fit?'Fit':'Crop'}`;
    $('uploadBtn').disabled=false;
  },'image/jpeg',0.82);
}

$('galleryPicker').addEventListener('change',e=>{
  const file=e.target.files[0];
  if(!file)return;
  croppedBlob=null;
  $('uploadZoneWrap').style.display='none';
  $('cropArea').style.display='';
  $('uploadBtn').disabled=true;
  $('uploadStatus').textContent='';
  $('imageFitMode').value='crop';
  $('photoName').value=file.name.replace(/\.[^/.]+$/,'').replace(/[^a-zA-Z0-9_\-]/g,'_').substr(0,20);
  if(galleryImageUrl)URL.revokeObjectURL(galleryImageUrl);
  const img=new Image();
  galleryImageUrl=URL.createObjectURL(file);
  img.onload=()=>{galleryImage=img;URL.revokeObjectURL(galleryImageUrl);galleryImageUrl=null;renderGalleryPreview();};
  img.onerror=()=>{toast('Could not read this image',true);resetCrop();};
  img.src=galleryImageUrl;
});

async function uploadCroppedPhoto(){
  if(!croppedBlob)return;
  const btn=$('uploadBtn'),status=$('uploadStatus'),prog=$('uploadProgress'),bar=$('uploadBar');
  let name=($('photoName').value||'photo_'+Date.now()).trim().replace(/[^a-zA-Z0-9_\-]/g,'_');
  if(!name.toLowerCase().endsWith('.jpg'))name+='.jpg';
  btn.disabled=true;prog.style.display='';bar.style.width='0%';
  status.innerHTML=`<span style="color:var(--blue)">Uploading...</span>`;
  const fd=new FormData();fd.append('file',croppedBlob,name);
  const xhr=new XMLHttpRequest();xhr.open('POST','/api/gallery/upload');
  xhr.upload.onprogress=e=>{if(e.lengthComputable)bar.style.width=Math.round(e.loaded/e.total*100)+'%';};
  xhr.onload=()=>{
    let b={};try{b=JSON.parse(xhr.responseText)}catch{}
    prog.style.display='none';
    if(xhr.status>=200&&xhr.status<300&&b.ok){
      toast('Photo uploaded!');
      status.innerHTML=`<span style="color:var(--green)">\u2713 "${name}" uploaded</span>`;
      loadGalleryFiles();setTimeout(resetCrop,1500);
    } else {
      status.innerHTML=`<span style="color:var(--red)">Failed: ${esc(b.error||'unknown')}</span>`;
      btn.disabled=false;
    }
  };
  xhr.onerror=()=>{status.innerHTML=`<span style="color:var(--red)">Upload error</span>`;btn.disabled=false;prog.style.display='none';};
  xhr.send(fd);
}

async function loadGalleryFiles(){
  const grid=$('galleryGrid'),info=$('fsInfo');
  if(!grid)return;
  grid.innerHTML=`<div style="color:var(--muted);font-size:13px;text-align:center;padding:24px;grid-column:1/-1">Loading...</div>`;
  try{
    const data=await api('/api/gallery/files');
    if(!data.ok){grid.innerHTML=`<div style="color:var(--red);text-align:center;padding:16px;grid-column:1/-1">Failed to load files</div>`;return;}
    const usedKb=(data.used/1024).toFixed(1),totalKb=(data.total/1024).toFixed(1),pct=data.total>0?((data.used/data.total)*100).toFixed(0):0;
    if(info)info.innerHTML=`${usedKb} KB / ${totalKb} KB &bull; ${data.files.length} photo(s) (${pct}%)`;
    if(!data.files||data.files.length===0){
      grid.innerHTML=`<div style="color:var(--muted);text-align:center;padding:32px;grid-column:1/-1">${icon('image')}<br><br>No photos yet. Upload one above.</div>`;
      return;
    }
    grid.innerHTML=data.files.map(f=>`
      <div class="gallery-item">
        <img src="/api/gallery/file?name=${encodeURIComponent(f.name)}" loading="lazy" alt="${esc(f.name)}">
        <div class="gi-foot">
          <span class="gi-name" title="${esc(f.name)}">${esc(f.name)}</span>
          <button class="gi-del" title="Delete" data-photo="${esc(encodeURIComponent(f.name))}">${icon('trash')}</button>
        </div>
      </div>`).join('');
    grid.querySelectorAll('[data-photo]').forEach(button=>button.onclick=()=>deletePhoto(decodeURIComponent(button.dataset.photo)));
  } catch(e){
    if(grid)grid.innerHTML=`<div style="color:var(--red);text-align:center;padding:16px;grid-column:1/-1">Error: ${esc(e.message)}</div>`;
  }
}

async function deletePhoto(name){
  if(!confirm(`Delete "${name}" from LittleFS?`))return;
  try{const d=await api(`/api/gallery/delete?name=${encodeURIComponent(name)}`);if(d.ok){toast('Deleted');loadGalleryFiles();}else toast(d.error||'Delete failed',true);}
  catch(e){toast(e.message,true);}
}

// ── Settings ──────────────────────────────────────────────────────────────────
function renderSettings(){
  $('view-settings').innerHTML=`
    <div class="section-head"><div><h2>Settings</h2><p>Theme, display and maintenance options.</p></div></div>
    <div class="form-section">
      <div class="form-title"><h3>Appearance</h3><p>Theme changes apply immediately on device and web UI.</p></div>
      <div style="padding:14px 15px;display:flex;gap:10px">
        <button class="btn" onclick="applyTheme('dark')">${icon('moon')} Dark</button>
        <button class="btn" onclick="applyTheme('light')">${icon('sun')} Light</button>
      </div>
    </div>
    <div class="form-section">
      <div class="form-title"><h3>System</h3><p>Maintenance and OTA update.</p></div>
      <div style="padding:14px 15px;display:flex;gap:10px;flex-wrap:wrap">
        <a class="btn" href="/ota">${icon('upload')} OTA Update</a>
        <button class="btn" onclick="fetch('/api/reset').then(()=>toast('Drivers reset'))">Reset Drivers</button>
        <button class="btn danger" onclick="rebootDevice()">${icon('reboot')} Reboot Device</button>
      </div>
    </div>`;
}

async function renderSystem(){
  const root=$('view-system');
  root.innerHTML='<div class="section-head"><div><h2>System</h2><p>Loading live device telemetry...</p></div></div>';
  try{
    const [h,p,fs]=await Promise.all([api('/api/health'),api('/api/power'),api('/api/gallery/files').catch(()=>({used:0,total:0}))]);
    const battery=p.battery_present?(p.percentage+'% estimate'):'Not detected';
    const source=!p.battery_present?'External power':p.charging?'Charging (USB present)':'USB presence unknown';
    root.innerHTML=`
      <div class="section-head"><div><h2>System</h2><p>Live health, power policy and maintenance for PoKo.</p></div><button class="btn" onclick="renderSystem()">${icon('refresh')} Refresh</button></div>
      <div class="metrics">
        <div class="metric"><small>Battery (voltage estimate)</small><strong>${esc(battery)}</strong><span>${p.battery_present?Number(p.voltage).toFixed(2)+' V':'No battery'}</span></div>
        <div class="metric"><small>Power</small><strong style="font-size:16px">${esc(source)}</strong><span>${p.full?'Battery near full':p.critical?'Critical voltage':p.low?'Low voltage':'Voltage-based estimate'}</span></div>
        <div class="metric"><small>Display</small><strong>${esc(p.display_state)}</strong><span>CPU ${h.cpu_mhz} MHz</span></div>
        <div class="metric"><small>Storage</small><strong>${fmtBytes(fs.used||0)}</strong><span>${fmtBytes(fs.total||0)} capacity</span></div>
      </div>
      <div class="columns">
        <div class="form-section"><div class="form-title"><h3>${icon('battery')} Power policy</h3><p>Timeout values of 0 disable the corresponding automatic action.</p></div>
          <form id="systemPowerForm" class="form-grid">
            <div class="field"><label>Dim after<small>Seconds without input</small></label><input type="number" name="dim_timeout" min="0" max="604800" value="${p.dim_timeout}" required></div>
            <div class="field"><label>Sleep after<small>Seconds without input</small></label><input type="number" name="sleep_timeout" min="0" max="604800" value="${p.sleep_timeout}" required></div>
            <div class="field"><label>Low-battery auto-off<small>Seconds continuously below threshold</small></label><input type="number" name="auto_off" min="0" max="604800" value="${p.auto_off}" required></div>
            <div class="field"><label>Auto-off threshold<small>Battery percent; 0 disables</small></label><input type="number" name="auto_off_battery_pct" min="0" max="100" value="${p.auto_off_battery_pct}" required></div>
            <div class="field"><span class="label">Ambient clock</span><label class="switch"><input name="ambient_clock" type="checkbox" ${p.ambient_clock?'checked':''}><span></span></label></div>
            <div class="field"><span class="label">USB max performance<small>Only when power source can be detected</small></span><label class="switch"><input name="usb_perf" type="checkbox" ${p.usb_perf_max?'checked':''}><span></span></label></div>
            <div class="field"><span class="label">Allow Wi-Fi sleep<small>Lower idle power, except during realtime audio</small></span><label class="switch"><input name="wifi_sleep" type="checkbox" ${p.wifi_sleep_allowed?'checked':''}><span></span></label></div>
            <div class="form-foot" style="grid-column:1/-1"><button class="btn blue" type="submit">${icon('save')} Save power policy</button></div>
          </form>
        </div>
        <div>
          <div class="form-section"><div class="form-title"><h3>${icon('system')} Device details</h3></div><div class="panel-body status-list">
            <div class="status-row"><span>App</span><strong>${esc(APP_NAMES[h.app_state]||'Unknown')}</strong></div>
            <div class="status-row"><span>Firmware</span><strong>${esc(h.firmware_version||'Unknown')}</strong></div>
            <div class="status-row"><span>IP / SSID</span><strong>${esc(h.ip||'—')} · ${esc(h.ssid||'—')}</strong></div>
            <div class="status-row"><span>Heap / PSRAM</span><strong>${fmtBytes(h.heap_free)} / ${fmtBytes(h.psram_free)}</strong></div>
            <div class="status-row"><span>Uptime</span><strong>${fmtUptime(h.uptime_ms)}</strong></div>
            <div class="status-row"><span>Locks</span><strong>${Number(p.locks)||0}</strong></div>
            <div class="status-row"><span>Audio amplifier</span><strong>${p.speaker_amp?'Active':'Standby'}</strong></div>
          </div></div>
          <div class="form-section"><div class="form-title"><h3>Display &amp; maintenance</h3></div><div class="panel-body actions">
            <button class="btn" data-screen="on">Wake display</button><button class="btn" data-screen="dim">Dim</button><button class="btn" data-screen="off">Sleep display</button>
            <a class="btn" href="/ota">${icon('upload')} OTA updates</a>
            <button class="btn danger" onclick="rebootDevice()">${icon('reboot')} Reboot</button>
          </div></div>
        </div>
      </div>`;
    $('systemPowerForm').onsubmit=savePowerPolicy;
    root.querySelectorAll('[data-screen]').forEach(button=>button.onclick=async()=>{try{await api('/api/power?screen='+button.dataset.screen);toast('Display updated');renderSystem()}catch(e){toast(e.message,true)}});
  }catch(e){root.innerHTML=`<div class="form-section"><div class="panel-body" style="color:var(--red)">${esc(e.message)}</div></div>`}
}
async function savePowerPolicy(e){
  e.preventDefault();
  const form=e.currentTarget,values=new FormData(form),query=new URLSearchParams();
  for(const key of ['dim_timeout','sleep_timeout','auto_off','auto_off_battery_pct'])query.set(key,values.get(key));
  for(const key of ['ambient_clock','usb_perf','wifi_sleep'])query.set(key,values.has(key)?'1':'0');
  try{await api('/api/power?'+query);toast('Power policy saved');renderSystem()}catch(err){toast(err.message,true)}
}

// ── Common controls ───────────────────────────────────────────────────────────
let brTimer=null;
function setBrightness(v){$('brVal').textContent=v+'%';clearTimeout(brTimer);brTimer=setTimeout(()=>fetch('/api/sys?brightness='+v),100);}
let masterTimer=null;
function setMasterVolume(v){$('masterVolVal').textContent=v+'%';clearTimeout(masterTimer);masterTimer=setTimeout(()=>fetch('/api/sys?master_vol='+v),100);}
let volTimer=null;
function setAppVolume(v){$('volVal').textContent=v+'%';clearTimeout(volTimer);volTimer=setTimeout(()=>fetch('/api/sys?volume='+v),100);}
let ampBoostTimer=null;
function setAmpBoost(v){$('ampBoostVal').textContent='+'+v+' dB';clearTimeout(ampBoostTimer);ampBoostTimer=setTimeout(()=>fetch('/api/sys?amp_boost='+v),100);}
function setSnapVolume(v){fetch('/api/snap?vol='+v);}
function toggleSnapMute(){fetch('/api/snap?mute=1');}
function setLED(r,g,b){fetch(`/api/led?r=${r}&g=${g}&b=${b}`);}
function pickLEDColor(hex){
  const r=parseInt(hex.substr(1,2),16),g=parseInt(hex.substr(3,2),16),b=parseInt(hex.substr(5,2),16);
  if($('pxRSlider')){$('pxRSlider').value=r;$('pxGSlider').value=g;$('pxBSlider').value=b;onRgbChange();}
  else{setLED(r,g,b);}
}
let pxRgbTimer=null;
function onRgbChange(){
  const r=+$('pxRSlider').value,g=+$('pxGSlider').value,b=+$('pxBSlider').value;
  $('pxRVal').textContent=r;$('pxGVal').textContent=g;$('pxBVal').textContent=b;
  const hex='#'+[r,g,b].map(x=>x.toString(16).padStart(2,'0')).join('');
  if($('netSwatch'))$('netSwatch').style.background=hex;
  if($('netHex'))$('netHex').textContent=hex.toUpperCase();
  if($('netRgb'))$('netRgb').textContent=`R:${r} G:${g} B:${b}`;
  if($('ledColorPicker'))$('ledColorPicker').value=hex;
  clearTimeout(pxRgbTimer);
  pxRgbTimer=setTimeout(()=>fetch(`/api/pixels?r=${r}&g=${g}&b=${b}&mode=solid`),80);
}
function setPixelMode(m){fetch('/api/pixels?mode='+m).then(()=>toast('Mode: '+m));}
let curTargetMask=255;
function toggleLed(i){
  if(healthCache&&healthCache.target_mask!==undefined)curTargetMask=healthCache.target_mask;
  curTargetMask^=(1<<i);
  fetch(`/api/pixels?target_mask=${curTargetMask}&mode=solid`)
    .then(r=>r.json())
    .then(d=>{
      if(healthCache){healthCache.target_mask=d.target_mask;healthCache.target_label=d.target_label;healthCache.pixel_mode=d.mode;}
      toast(`LED ${i+1} ${(curTargetMask&(1<<i))?'ON':'OFF'}`);
      renderControls();
    });
}
function setAllLeds(on){
  curTargetMask=on?255:0;
  fetch(`/api/pixels?target_mask=${curTargetMask}${on?'&mode=solid':''}`)
    .then(r=>r.json())
    .then(d=>{
      if(healthCache){healthCache.target_mask=d.target_mask;healthCache.target_label=d.target_label;if(on)healthCache.pixel_mode=d.mode;}
      toast(on?'All 8 LEDs selected':'All LEDs cleared');
      renderControls();
    });
}
function setPixelTarget(t){fetch('/api/pixels?target='+t+'&mode=solid').then(()=>{toast('Target: '+(t===8?'All 8':'LED '+(t+1)));renderControls();});}
let pxBrTimer=null;
function setPixelBrightness(v){$('pxBrightVal').textContent=Math.round(v*100/255)+'%';clearTimeout(pxBrTimer);pxBrTimer=setTimeout(()=>fetch('/api/pixels?brightness='+v),80);}
function setMusicLight(on){fetch('/api/pixels?music_light='+(on?1:0));}
function setMusicEffect(fx){fetch('/api/pixels?music_effect='+fx).then(()=>toast('Music FX: '+fx));}
function setSSyncLight(on){fetch('/api/pixels?ssync_light='+(on?1:0));}
function setSSyncEffect(fx){fetch('/api/pixels?ssync_effect='+fx).then(()=>toast('SSync FX: '+fx));}
function setFreqResponse(fr){fetch('/api/pixels?freq_resp='+fr).then(()=>toast('Freq Resp: '+fr));}
function applyQuickColor(r,g,b){
  if($('pxRSlider')){$('pxRSlider').value=r;$('pxGSlider').value=g;$('pxBSlider').value=b;onRgbChange();}
}
function setSlideTimer(v){fetch('/api/gallery?timer='+v);}
function rebootDevice(){if(confirm('Reboot PoKo now?'))fetch('/api/reboot').then(()=>toast('Rebooting...'));}
async function saveSnap(e){
  e.preventDefault();
  const h=$('snap_host').value.trim(),p=$('snap_port').value.trim();
  if(!h)return;
  try{
    await api(`/api/snap?host=${encodeURIComponent(h)}&port=${encodeURIComponent(p)}`);
    if(healthCache){healthCache.snap_host=h;healthCache.snap_port=p;}
    toast('Snapcast updated to '+h+':'+p);
  }catch(err){
    toast('Update failed: '+err.message,true);
  }
}
async function saveMediaServer(e){
  e.preventDefault();
  const a=$('server_addr').value.trim();
  if(!a)return;
  try{
    await api(`/api/server?addr=${encodeURIComponent(a)}`);
    if(healthCache){healthCache.server_addr=a;}
    toast('Media server updated');
  }catch(err){
    toast('Update failed: '+err.message,true);
  }
}
async function saveWiFi(e){e.preventDefault();const s=$('wifi_ssid').value,p=$('wifi_pass').value;try{await api('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid:s,pass:p})});toast('Wi-Fi saved — reconnecting...');setTimeout(()=>location.reload(),4000)}catch(err){toast(err.message,true)}}

// ── Render dispatch ───────────────────────────────────────────────────────────
function renderView(){
  switch(activeView){
    case 'dashboard': renderDashboard();break;
    case 'apps': renderApps();break;
    case 'controls': renderControls();break;
    case 'gallery': renderGallery();break;
    case 'settings': renderSettings();break;
    case 'system': renderSystem();break;
  }
}

function refreshView(){renderView();}

// ── Periodic refresh ──────────────────────────────────────────────────────────
async function pollHealth(){
  try{
    const d=await api('/api/health');
    healthCache=d;
    if((d.theme==='dark'||d.theme==='light')&&d.theme!==curTheme)setTheme(d.theme);
    setOnline(true);
    // Update dashboard live if visible
    if(activeView==='dashboard')syncDashboard(d);
    // Update apps view live if visible
    if(activeView==='apps')syncAppsView(d);
    // Update controls sliders and live snap player status
    if(activeView==='controls'){
      const brEl=$('brSlider');if(brEl&&!brEl.matches(':active')&&d.brightness!=null){brEl.value=d.brightness;$('brVal').textContent=d.brightness+'%';}
      const mvEl=$('masterVolSlider');if(mvEl&&!mvEl.matches(':active')&&d.master_vol!=null){mvEl.value=d.master_vol;$('masterVolVal').textContent=d.master_vol+'%';}
      const volEl=$('volSlider');if(volEl&&!volEl.matches(':active')&&d.app_vol!=null){volEl.value=d.app_vol;$('volVal').textContent=d.app_vol+'%';}
      api('/api/snap').then(s=>{
        if(!s)return;
        liveText('snapStatus',s.connected?(s.playing?'PLAYING':'CONNECTED'):'OFFLINE');
        liveText('snapCodec',s.codec||'--');
        const snapVol=$('snapVolSlider');
        if(snapVol&&!snapVol.matches(':active')&&s.volume!=null)snapVol.value=s.volume;
        const muteBtn=$('muteBtn');
        if(muteBtn)muteBtn.textContent=s.muted?'Unmute':'Mute';
      }).catch(()=>{});
    }
    // Gallery timer
    if(d.gallery_timer!=null){const sel=$('slideTimerSel');if(sel)sel.value=String(d.gallery_timer);}
  }catch(e){setOnline(false);}
}

// ── Init ──────────────────────────────────────────────────────────────────────
(function init(){
  const saved=localStorage.getItem('poko-theme');
  if(saved)setTheme(saved);
  setupChrome();
  const hash=(location.hash||'').replace('#','');
  navigate(navItems.find(x=>x[0]===hash)?hash:'dashboard');
  setInterval(pollHealth,3000);
  pollHealth();
})();
</script>
</body>
</html>
)POKOHTML";
