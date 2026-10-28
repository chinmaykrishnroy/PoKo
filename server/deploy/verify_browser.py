"""Opt-in login/tab-return/logout checks. Targets and secrets come from --env."""
import concurrent.futures
from http.cookies import SimpleCookie
import time
import urllib.error
import urllib.parse
import urllib.request

from verification_config import verification_parser, load_verification_env, target_url, secret_directory

args = verification_parser(__doc__).parse_args()
settings = load_verification_env(args.env)
root = secret_directory(settings)

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args):
        return None

def request(base, path, *, cookie="", data=None, method=None):
    headers = {"Sec-Fetch-Mode": "cors", "Origin": base, "User-Agent": "ESP32HTTPClient"}
    if cookie:
        headers["Cookie"] = cookie
    body = urllib.parse.urlencode(data).encode() if data is not None else None
    req = urllib.request.Request(base + path, data=body, headers=headers, method=method)
    try:
        response = urllib.request.build_opener(NoRedirect).open(req, timeout=20)
    except urllib.error.HTTPError as exc:
        response = exc
    with response:
        return response.status, response.headers, response.read()

for kind, base, filename, health in [
    ("server", target_url(settings, "POKO_TEST_SERVER_URL", https=True), "admin-password", "/health"),
    ("device", target_url(settings, "POKO_TEST_DEVICE_URL"), "device-admin-password", "/api/health"),
]:
    password = (root / filename).read_text().strip()
    status, headers, _ = request(base, "/")
    assert status == 303 and headers.get("Location") == "/login", (kind, status)
    assert "WWW-Authenticate" not in headers
    status, headers, _ = request(base, "/api/security/login", data={"username": "admin", "password": password, "remember": "1"})
    assert status == 303, (kind, status)
    raw = headers.get("Set-Cookie", "")
    assert "HttpOnly" in raw and "SameSite=Strict" in raw and "Max-Age=2592000" in raw, kind
    if kind == "server":
        assert "Secure" in raw
    jar = SimpleCookie()
    jar.load(raw)
    cookie = "; ".join(key + "=" + value.value for key, value in jar.items())
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            results = list(pool.map(lambda _: request(base, health, cookie=cookie), range(12)))
        assert all(result[0] == 200 and "WWW-Authenticate" not in result[1] for result in results), kind
        if kind == "device" and settings.get("POKO_TEST_REBOOT") == "1":
            assert request(base, "/api/reboot", cookie=cookie)[0] == 200
            time.sleep(12)
            for attempt in range(12):
                try:
                    if request(base, health, cookie=cookie)[0] == 200:
                        break
                except OSError:
                    pass
                if attempt == 11:
                    raise AssertionError("Remembered device login did not survive reboot/clock sync")
                time.sleep(2)
            print("PASS remembered device session survives reboot")
    finally:
        status, _, _ = request(base, "/api/security/logout", cookie=cookie, data={})
        assert status == 303, (kind, status)
    status, headers, _ = request(base, health, cookie=cookie)
    assert status == 401 and "WWW-Authenticate" not in headers, (kind, status)
    print("PASS", kind, "login, parallel tab returns, Remember me, logout and replay rejection")
