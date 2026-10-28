#include "DeviceWebSession.h"
#include <Preferences.h>
#include <esp_system.h>
#include <time.h>
#include <array>
#include <string>
extern Preferences prefs;

namespace {
struct Session { String token; uint32_t created = 0; uint64_t expires = 0; bool remembered = false; };
std::array<Session, 4> sessions;
bool loaded = false;
unsigned nextSlot = 0;
unsigned failures = 0;
uint32_t failureWindow = 0;
uint64_t currentEpoch() {
    time_t now = time(nullptr);
    return now >= 1700000000 && (uint64_t)now < 4102444800ULL ? (uint64_t)now : 0;
}
const char* loginHtml = R"LOGIN(<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><title>Sign in · PoKo</title>
<style>body{font:16px system-ui;background:#11151b;color:#edf1f7;margin:40px auto;width:min(360px,85vw)}label{display:block;margin:18px 0 6px}input:not([type=checkbox]){box-sizing:border-box;width:100%;padding:12px;font:inherit}button{margin:24px 0;padding:12px;width:100%;font:inherit}p{line-height:1.5}</style>
<h1>Sign in to PoKo</h1><p>Your password is saved on the device. View it through Settings → Security on its screen.</p>
<form method="post" action="/api/security/login"><label>Username<input name="username" value="admin" autocomplete="username" required></label><label>Password<input name="password" type="password" autocomplete="current-password" required maxlength="128"></label>
<label><input type="checkbox" name="remember" value="1"> Remember me on this browser</label><button>Sign in</button></form><p>Remembering across reboots requires clock sync. Use this device dashboard only on your trusted LAN.</p>)LOGIN";

String keyFor(const char* prefix, unsigned slot) {
    char key[12]; snprintf(key, sizeof(key), "%s%u", prefix, slot); return String(key);
}
void eraseSlot(unsigned slot) {
    sessions[slot] = Session{};
    prefs.remove(keyFor("ws_t", slot).c_str());
    prefs.remove(keyFor("ws_e", slot).c_str());
}
void loadSessions() {
    uint64_t now = currentEpoch();
    if (loaded || now < 1700000000) return;
    loaded = true;
    for (unsigned i = 0; i < sessions.size(); ++i) {
        if (sessions[i].token.length()) continue;
        String token = prefs.getString(keyFor("ws_t", i).c_str(), "");
        uint64_t expiry = prefs.getULong64(keyFor("ws_e", i).c_str(), 0);
        if (token.length() == 64 && expiry > now && expiry - now <= 31 * 86400ULL)
            sessions[i] = {token, (uint32_t)millis(), expiry, true};
        else if (token.length()) eraseSlot(i);
    }
}
String cookieToken(WebServer& server) {
    String rawCookie = server.header("Cookie");
    if (rawCookie.length() > 4096) return "";
    std::string cookie(rawCookie.c_str());
    size_t offset = 0;
    while (offset < cookie.size()) {
        size_t end = cookie.find(';', offset);
        if (end == std::string::npos) end = cookie.size();
        while (offset < end && cookie[offset] == ' ') ++offset;
        const std::string name = "poko_device_session=";
        if (cookie.compare(offset, name.size(), name) == 0 && end - offset == name.size() + 64)
            return String(cookie.substr(offset + name.size(), 64).c_str());
        offset = end + 1;
    }
    return "";
}
void redirect(WebServer& server, const char* path) {
    server.sendHeader("Location", path);
    server.send(303, "text/plain", "");
}
void issueSession(WebServer& server, bool remember) {
    loadSessions();
    unsigned slot = nextSlot++ % sessions.size();
    for (unsigned i = 0; i < sessions.size(); ++i) {
        if (!sessions[i].token.length()) { slot = i; break; }
    }
    eraseSlot(slot);
    char token[65];
    for (unsigned i = 0; i < 8; ++i) snprintf(token + i * 8, 9, "%08lx", (unsigned long)esp_random());
    uint64_t now = currentEpoch();
    remember = remember && now >= 1700000000;
    sessions[slot] = {String(token), (uint32_t)millis(), remember ? now + 30 * 86400ULL : 0, remember};
    String cookie = "poko_device_session=" + String(token);
    cookie = cookie + "; Path=/; HttpOnly; SameSite=Strict";
    if (remember) {
        // If persistence fails, the current boot's session still works, but do
        // not advertise a persistent browser cookie.
        bool saved = prefs.putString(keyFor("ws_t", slot).c_str(), sessions[slot].token) == 64 &&
                     prefs.putULong64(keyFor("ws_e", slot).c_str(), sessions[slot].expires) == sizeof(uint64_t);
        if (saved) cookie = cookie + "; Max-Age=2592000";
        else { prefs.remove(keyFor("ws_t", slot).c_str()); prefs.remove(keyFor("ws_e", slot).c_str()); }
    }
    server.sendHeader("Set-Cookie", cookie.c_str());
}
}

bool deviceBrowserRequest(WebServer& server) {
    return server.header("Sec-Fetch-Mode").length() || server.header("Accept").indexOf("text/html") >= 0;
}

bool deviceBrowserSessionAuthorized(WebServer& server) {
    loadSessions();
    String token = cookieToken(server);
    if (token.length() != 64) return false;
    uint64_t now = currentEpoch();
    for (unsigned i = 0; i < sessions.size(); ++i) {
        auto& session = sessions[i];
        if (!session.token.length()) continue;
        bool valid = session.remembered ? (now >= 1700000000 && now < session.expires)
                                        : (uint32_t)(millis() - session.created) < 12 * 3600000UL;
        if (!valid) { eraseSlot(i); continue; }
        if (session.token.equalsConstantTime(token)) return true;
    }
    return false;
}

void clearDeviceBrowserSessions() {
    for (unsigned i = 0; i < sessions.size(); ++i) eraseSlot(i);
}

bool deviceBrowserSessionPendingClock(WebServer& server) {
    if (currentEpoch()) return false;
    String token = cookieToken(server);
    if (token.length() != 64) return false;
    for (unsigned i = 0; i < sessions.size(); ++i) {
        if (prefs.getString(keyFor("ws_t", i).c_str(), "").equalsConstantTime(token) &&
            prefs.getULong64(keyFor("ws_e", i).c_str(), 0) >= 1700000000) return true;
    }
    return false;
}

void installDeviceBrowserSessions(WebServer& server) {
    server.on("/login", HTTP_GET, [&server]() {
        if (deviceBrowserSessionAuthorized(server)) { redirect(server, "/"); return; }
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "text/html; charset=utf-8", loginHtml);
    });
    server.on("/api/security/login", HTTP_POST, [&server]() {
        uint32_t now = millis();
        if (now - failureWindow > 60000) { failures = 0; failureWindow = now; }
        if (failures >= 10) { server.send(429, "text/plain", "Too many attempts. Wait one minute."); return; }
        String password = prefs.getString("device_pass", "");
        if (password.length() < 8 || server.arg("username") != "admin" ||
            !password.equalsConstantTime(server.arg("password"))) {
            ++failures;
            server.send(401, "text/html", "<p>Username or password is incorrect. <a href=/login>Try again</a></p>");
            return;
        }
        failures = 0;
        issueSession(server, server.arg("remember") == "1");
        redirect(server, "/");
    });
    server.on("/api/security/logout", HTTP_POST, [&server]() {
        String token = cookieToken(server);
        for (unsigned i = 0; i < sessions.size(); ++i)
            if (token.length() && sessions[i].token.equalsConstantTime(token)) eraseSlot(i);
        server.sendHeader("Set-Cookie", "poko_device_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
        redirect(server, "/login");
    });
}
