#include "DeviceSecurity.h"
#include <Preferences.h>
extern Preferences prefs;

bool deviceRequestAuthorized(WebServer& server, bool challenge) {
    String password = prefs.getString("device_pass", "");
    bool authorized = password.length() >= 16 && server.authenticate("admin", password.c_str());
    if (!authorized && challenge) server.requestAuthentication(DIGEST_AUTH, "PoKo");
    return authorized;
}

void installDeviceAuthentication(WebServer& server) {
    const char* headers[] = {"Origin", "Sec-Fetch-Site"};
    server.collectHeaders(headers, 2);
    server.addMiddleware([](WebServer& request, Middleware::Callback next) -> bool {
        // First pairing is a local, five-minute boot window. Once provisioned,
        // all routes (including OTA) require the separate device admin password.
        bool unpaired = prefs.getString("device_pass", "").length() == 0;
        if (request.header("Sec-Fetch-Site") == "cross-site") {
            request.send(403, "text/plain", "Cross-site request refused");
            return false;
        }
        String origin = request.header("Origin");
        if (origin.length() && origin != "http://" + request.hostHeader()) {
            request.send(403, "text/plain", "Origin refused");
            return false;
        }
        if (unpaired && request.uri() == "/api/security/setup" &&
            request.method() == HTTP_POST && millis() < 300000) return next();
        if (unpaired && request.uri() == "/" && request.method() == HTTP_GET && millis() < 300000) {
            request.sendHeader("Cache-Control", "no-store");
            request.send(200, "text/html", "<!doctype html><meta name=viewport content='width=device-width'><title>Pair PoKo</title><h1>Pair PoKo</h1><p>Set a device admin password (16+ characters). Pair only on your trusted local network. This setup closes five minutes after boot.</p><form method=post action=/api/security/setup><input name=password type=password minlength=16 maxlength=128 required autocomplete=new-password><button>Set password</button></form><p>After pairing, open / and sign in as admin.</p>");
            return false;
        }
        if (!deviceRequestAuthorized(request)) return false;
        request.sendHeader("Cache-Control", "no-store");
        return next();
    });
    server.on("/api/security/setup", HTTP_POST, [&server]() {
        String password = server.arg("password");
        if (password.length() < 16 || password.length() > 128) {
            server.send(400, "text/plain", "Password must contain 16 to 128 characters");
            return;
        }
        prefs.putString("device_pass", password);
        server.send(200, "application/json", "{\"ok\":true}");
    });
}
