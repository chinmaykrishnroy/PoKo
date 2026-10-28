SETUP_HTML = r"""<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Set up PoKo server</title>
<style>body{font:16px system-ui;background:#101418;color:#edf3f7;max-width:520px;margin:8vh auto;padding:20px}label,input,button{display:block;margin:12px 0;width:100%;box-sizing:border-box}input,button{padding:12px;border-radius:6px}button{cursor:pointer}#result{color:#ffacac}</style>
<h1>Choose your server password</h1><p>The initial password <code>00000000</code> only opens this setup page. Media, tokens and device controls stay locked until setup is complete.</p>
<p>Enter the one-time setup code saved on your server machine. With the example Linux service, run <code>sudo cat /var/lib/poko/setup-code</code>. For other installations, use the setup-code file in your credentials folder.</p>
<form id="setup"><input type="hidden" name="username" value="admin" autocomplete="username">
<label>Initial password<input type="password" name="current_password" autocomplete="current-password" required></label>
<label>One-time setup code<input name="setup_code" autocomplete="off" required maxlength="128"></label>
<label>New server passphrase (16–128 characters)<input type="password" name="password" autocomplete="new-password" required minlength="16" maxlength="128"></label>
<label>Confirm passphrase<input type="password" name="confirmation" autocomplete="new-password" required minlength="16" maxlength="128"></label>
<button>Save and sign in</button><p id="result" role="status"></p></form>
<script>document.getElementById('setup').onsubmit=async e=>{e.preventDefault();const data=Object.fromEntries(new FormData(e.currentTarget));try{const r=await fetch('/api/security/password',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});const value=await r.json();if(!r.ok)throw Error(value.error||'Setup failed');location.replace('/login')}catch(error){document.getElementById('result').textContent=error.message}};</script></html>"""
