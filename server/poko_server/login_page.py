LOGIN_HTML = """<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Sign in · PoKo</title>
<style>body{font:16px system-ui;background:#11151b;color:#edf1f7;margin:0;min-height:100vh;display:grid;place-items:center}main{width:min(360px,85vw)}h1{font-size:26px}label{display:block;margin:18px 0 6px}input:not([type=checkbox]){box-sizing:border-box;width:100%;padding:12px;border:1px solid #657184;border-radius:6px;background:#191f29;color:inherit;font:inherit}button{width:100%;margin:24px 0;padding:12px;font:inherit;border:0;border-radius:6px;background:#7fc4ff;color:#102032}p{color:#b8c2d0;line-height:1.5}</style>
<main><h1>Sign in to PoKo Server</h1><p>Use your server administrator login. Your password manager can save it.</p>
<form method="post" action="/api/security/login"><label for="username">Username</label><input id="username" name="username" value="admin" autocomplete="username" required>
<label for="password">Password</label><input id="password" name="password" type="password" autocomplete="current-password" required maxlength="128">
<label><input type="checkbox" name="remember" value="1"> Remember me on this browser</label>
<button>Sign in</button></form><p>__MESSAGE__</p></main></html>"""
