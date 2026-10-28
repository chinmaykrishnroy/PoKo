#include "DeviceSecurity.h"
#include "DeviceAccessPolicy.h"
#include "DeviceWebSession.h"
#include <Preferences.h>
extern Preferences prefs;

bool replaceDevicePassword(const String& password) {
    if (password.length() < 16 || password.length() > 128) return false;
    // Migrate the existing server credential before changing the human login.
    // It remains valid only for the explicit media-control allowlist.
    String previous = prefs.getString("device_pass", "");
    if (previous.length() && !prefs.getString("device_ctrl", "").length() &&
        prefs.putString("device_ctrl", previous) != previous.length()) return false;
    bool saved = prefs.putString("device_pass", password) == password.length();
    if (saved) clearDeviceBrowserSessions();
    return saved;
}

bool deviceRequestAuthorized(WebServer& server, bool challenge) {
    if (deviceBrowserSessionAuthorized(server)) return true;
    if (deviceBrowserSessionPendingClock(server)) {
        if (challenge) {
            server.sendHeader("Retry-After", "2");
            if (server.uri() == "/")
                server.send(503, "text/html", "<meta http-equiv=refresh content=2><p>Waiting for device clock sync. <a href=/login>Sign in for this boot</a></p>");
            else server.send(503, "application/json", "{\"ok\":false,\"error\":\"Device clock syncing; retry shortly\"}");
        }
        return false;
    }
    if (deviceBrowserRequest(server)) {
        if (challenge) {
            if (server.method() == HTTP_GET && (server.uri() == "/" || server.uri() == "/ota")) {
                server.sendHeader("Location", "/login");
                server.send(303, "text/plain", "");
            } else server.send(401, "application/json", "{\"ok\":false,\"error\":\"Sign in required\"}");
        }
        return false;
    }
    String password = prefs.getString("device_pass", "");
    bool authorized = password.length() >= 16 && server.authenticate("admin", password.c_str());
    // Scope alone never authorizes: the integration password must authenticate.
    // metadata-auth-required tests deny missing/wrong credentials on this route.
    if (!authorized && deviceControlRoute(server.uri().c_str(), server.method() == HTTP_GET, server.method() == HTTP_POST)) {
        String control = prefs.getString("device_ctrl", "");
        authorized = control.length() >= 16 && server.authenticate("admin", control.c_str());
    }
    if (!authorized && challenge) server.requestAuthentication(DIGEST_AUTH, "PoKo");
    return authorized;
}

void installDeviceAuthentication(WebServer& server) {
    const char* headers[] = {"Origin", "Sec-Fetch-Site", "Sec-Fetch-Mode", "Accept", "Cookie"};
    server.collectHeaders(headers, 5);
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
        if ((request.uri() == "/login" && request.method() == HTTP_GET) ||
            (request.uri() == "/api/security/login" && request.method() == HTTP_POST)) return next();
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
    installDeviceBrowserSessions(server);
    server.on("/api/security/setup", HTTP_POST, [&server]() {
        String password = server.arg("password");
        if (password.length() < 16 || password.length() > 128) {
            server.send(400, "text/plain", "Password must contain 16 to 128 characters");
            return;
        }
        // Initial pairing only; rotations must verify the current password.
        if (prefs.getString("device_pass", "").length()) {
            server.send(409, "text/plain", "Use Security > Change password");
            return;
        }
        if (!replaceDevicePassword(password)) {
            server.send(500, "text/plain", "Could not save password");
            return;
        }
        server.send(200, "application/json", "{\"ok\":true}");
    });
    server.on("/api/security/password", HTTP_POST, [&server]() {
        String current = server.arg("current_password");
        String password = server.arg("password");
        if (current != prefs.getString("device_pass", "")) {
            server.send(403, "text/plain", "Current password is incorrect");
            return;
        }
        if (password.length() < 16 || password.length() > 128 ||
            password != server.arg("confirmation") || password == current ||
            password == prefs.getString("device_ctrl", "")) {
            server.send(400, "text/plain", "Use a different 16-128 character password and matching confirmation");
            return;
        }
        if (!replaceDevicePassword(password)) {
            server.send(500, "text/plain", "Could not save password");
            return;
        }
        server.send(200, "application/json", "{\"ok\":true,\"sign_in_again\":true,\"reboot_for_arduino_ota\":true}");
    });
}
