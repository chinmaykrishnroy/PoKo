#include "DeviceSecurity.h"
#include "DeviceAccessPolicy.h"
#include <Preferences.h>
extern Preferences prefs;

bool replaceDevicePassword(const String& password) {
    if (password.length() < 16 || password.length() > 128) return false;
    // Migrate the existing Pi credential before changing the human login.
    // It remains valid only for the explicit media-control allowlist.
    String previous = prefs.getString("device_pass", "");
    if (previous.length() && !prefs.getString("device_ctrl", "").length() &&
        prefs.putString("device_ctrl", previous) != previous.length()) return false;
    return prefs.putString("device_pass", password) == password.length();
}

bool deviceRequestAuthorized(WebServer& server, bool challenge) {
    String password = prefs.getString("device_pass", "");
    bool authorized = password.length() >= 16 && server.authenticate("admin", password.c_str());
    if (!authorized && deviceControlRoute(server.uri().c_str(), server.method() == HTTP_GET)) {
        String control = prefs.getString("device_ctrl", "");
        authorized = control.length() >= 16 && server.authenticate("admin", control.c_str());
    }
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
