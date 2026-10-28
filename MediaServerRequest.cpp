#include "MediaServerRequest.h"
#include <Preferences.h>
#include <time.h>
extern Preferences prefs;

bool MediaServerRequest::begin(String url) {
    String token = prefs.getString("server_token", "");
    bool tls = prefs.getBool("server_tls", false);
    _http.setReuse(false);
    _http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (token.length() > 0 && !tls) return false;
    bool ok;
    if (tls) {
        // Certificate date verification needs SNTP. Fail closed while the
        // clock is unset, rather than bypassing certificate verification.
        if (time(nullptr) < 1700000000) return false;
        if (url.startsWith("http://")) url = "https://" + url.substring(7);
        _ca = prefs.getString("server_ca", "");
        if (_ca.length()) _secure.setCACert(_ca.c_str());
        else _secure.useBuiltinCACertBundle();
        _secure.setHandshakeTimeout(8);
        ok = _http.begin(_secure, url);
    } else {
        ok = _http.begin(_plain, url);
    }
    if (ok && token.length()) _http.addHeader("Authorization", "Bearer " + token);
    return ok;
}
int MediaServerRequest::GET() { return _http.GET(); }
void MediaServerRequest::end() { _http.end(); }
void MediaServerRequest::setConnectTimeout(int32_t ms) { _http.setConnectTimeout(ms); }
void MediaServerRequest::setTimeout(uint16_t ms) { _http.setTimeout(ms); }
Stream& MediaServerRequest::getStream() { return _http.getStream(); }
NetworkClient* MediaServerRequest::getStreamPtr() { return _http.getStreamPtr(); }
int MediaServerRequest::getSize() { return _http.getSize(); }
bool MediaServerRequest::connected() { return _http.connected(); }
