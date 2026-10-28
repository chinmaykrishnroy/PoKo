#include "DeviceSecurity.h"
#include "DeviceAccessPolicy.h"
#include "DeviceWebSession.h"
#include <Preferences.h>
extern Preferences prefs;

static bool validDevicePassword(const String& password) {
    if (password.length() != 8) return false;
    for (unsigned i = 0; i < 8; ++i)
        if ((unsigned char)password[i] < 33 || (unsigned char)password[i] > 126) return false;
    return true;
}

bool replaceDevicePassword(const String& password) {
    if (!validDevicePassword(password)) return false;
    // Migrate the existing server credential before changing the human login.
    // It remains valid only for the explicit media-control allowlist.
    String previous = prefs.getString("device_pass", "");
    if (previous.length() && previous != "00000000" && !prefs.getString("device_ctrl", "").length() &&
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
    bool authorized = password.length() >= 8 && server.authenticate("admin", password.c_str());
    // Scope alone never authorizes: the integration password must authenticate.
    // metadata-auth-required tests deny missing/wrong credentials on this route.
    if (!authorized && deviceControlRoute(server.uri().c_str(), server.method() == HTTP_GET, server.method() == HTTP_POST)) {
        String control = prefs.getString("device_ctrl", "");
        authorized = control.length() >= 8 && control != "00000000" && server.authenticate("admin", control.c_str());
    }
    if (!authorized && challenge) server.requestAuthentication(DIGEST_AUTH, "PoKo");
    return authorized;
}

void installDeviceAuthentication(WebServer& server) {
    // Bootstrap only an unprovisioned device; firmware upgrades preserve NVS.
    if (!prefs.getString("device_pass", "").length()) prefs.putString("device_pass", "00000000");
    const char* headers[] = {"Origin", "Sec-Fetch-Site", "Sec-Fetch-Mode", "Accept", "Cookie"};
    server.collectHeaders(headers, 5);
    server.addMiddleware([](WebServer& request, Middleware::Callback next) -> bool {
        // All routes except the login page/form require device authentication.
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
        if (!deviceRequestAuthorized(request)) return false;
        request.sendHeader("Cache-Control", "no-store");
        return next();
    });
    installDeviceBrowserSessions(server);
    server.on("/api/security/setup", HTTP_POST, [&server]() {
        server.send(409, "text/plain", "Sign in and use Security > Change password");
    });
    server.on("/api/security/password", HTTP_POST, [&server]() {
        String current = server.arg("current_password");
        String password = server.arg("password");
        if (current != prefs.getString("device_pass", "")) {
            server.send(403, "text/plain", "Current password is incorrect");
            return;
        }
        if (!validDevicePassword(password) ||
            password != server.arg("confirmation") || password == current ||
            password == prefs.getString("device_ctrl", "")) {
            server.send(400, "text/plain", "Use a different 8-character password and matching confirmation");
            return;
        }
        if (!replaceDevicePassword(password)) {
            server.send(500, "text/plain", "Could not save password");
            return;
        }
        server.send(200, "application/json", "{\"ok\":true,\"sign_in_again\":true,\"reboot_for_arduino_ota\":true}");
    });
}
