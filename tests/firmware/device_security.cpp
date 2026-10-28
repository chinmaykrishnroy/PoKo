#include "security_stubs/WebServer.h"
#include "security_stubs/Preferences.h"
#include "../../DeviceSecurity.h"
#include "../../DeviceAccessPolicy.h"
#include "../../DeviceWebSession.h"
#include <cassert>
#include <iostream>
Preferences prefs;
int main(int argc, char** argv) {
    assert(argc == 2);
    std::string test = argv[1];
    String oldPassword = "old-secret-password-000", newPassword = "new-secret-password-111";
    prefs.putString("device_pass", oldPassword);
    WebServer server;
    installDeviceAuthentication(server);
    if (test == "rotation-preserves-controls") {
        assert(replaceDevicePassword(newPassword));
        server.supplied = oldPassword;
        for (const char* path : {"/api/health", "/api/app", "/api/ui/playback_stopped", "/api/sys"}) {
            server.path = path; assert(deviceRequestAuthorized(server, false));
        }
        for (const char* path : {"/", "/ota", "/ota/upload", "/api/server", "/api/security/password", "/api/input", "/api/reset"}) {
            server.path = path; assert(!deviceRequestAuthorized(server, false));
        }
        server.path = "/api/app"; server.verb = HTTP_POST;
        assert(!deviceRequestAuthorized(server, false));
        server.supplied = newPassword; server.path = "/ota/upload";
        assert(deviceRequestAuthorized(server, false));
        assert(replaceDevicePassword("third-secret-password-222"));
        assert(prefs.getString("device_ctrl", "") == oldPassword);
    } else if (test == "password-change-validation") {
        server.path = "/api/security/password"; server.verb = HTTP_POST;
        server.supplied = oldPassword;
        server.arguments = {{"current_password", "wrong"}, {"password", newPassword}, {"confirmation", newPassword}};
        server.dispatch(); assert(server.status == 403);
        server.arguments["current_password"] = oldPassword;
        server.arguments["confirmation"] = "mismatch";
        server.dispatch(); assert(server.status == 400);
        server.arguments["confirmation"] = newPassword;
        server.dispatch(); assert(server.status == 200);
        assert(prefs.getString("device_pass", "") == newPassword);
        server.supplied = oldPassword;
        server.dispatch(); assert(server.status == 401);
    } else if (test == "storage-failure-keeps-admin") {
        prefs.failWrite = true;
        assert(!replaceDevicePassword(newPassword));
        assert(prefs.getString("device_pass", "") == oldPassword);
    } else if (test == "setup-cannot-rotate") {
        server.path = "/api/security/setup"; server.verb = HTTP_POST;
        server.supplied = oldPassword; server.arguments["password"] = newPassword;
        server.dispatch(); assert(server.status == 409);
        assert(prefs.getString("device_pass", "") == oldPassword);
    } else if (test == "browser-no-native-prompt") {
        server.headers["Sec-Fetch-Mode"] = "cors";
        server.path = "/api/health"; server.supplied = oldPassword;
        assert(!deviceRequestAuthorized(server)); assert(server.status == 401);
        assert(server.responseHeaders.count("WWW-Authenticate") == 0);
        server.path = "/";
        assert(!deviceRequestAuthorized(server)); assert(server.status == 303);
        assert(server.responseHeaders["Location"] == "/login");
    } else if (test == "browser-session-tab-return" || test == "browser-session-password-revocation" || test == "browser-remember-cookie") {
        server.headers["Sec-Fetch-Mode"] = "navigate";
        server.path = "/api/security/login"; server.verb = HTTP_POST;
        server.arguments = {{"username", "admin"}, {"password", oldPassword}, {"remember", test == "browser-remember-cookie" ? "1" : "0"}};
        server.dispatch(); assert(server.status == 303);
        String cookie = server.responseHeaders["Set-Cookie"];
        assert(cookie.indexOf("HttpOnly") >= 0 && cookie.indexOf("SameSite=Strict") >= 0);
        assert((cookie.indexOf("Max-Age") >= 0) == (test == "browser-remember-cookie"));
        server.headers["Cookie"] = cookie;
        server.path = "/api/health"; server.verb = HTTP_GET;
        for (int tab = 0; tab < 30; ++tab) assert(deviceRequestAuthorized(server));
        if (test == "browser-session-password-revocation") {
            assert(replaceDevicePassword(newPassword));
            assert(!deviceRequestAuthorized(server, false));
        } else {
            server.path = "/api/security/logout"; server.verb = HTTP_POST;
            server.dispatch(); assert(server.status == 303);
            assert(!deviceBrowserSessionAuthorized(server));
        }
    } else return 2;
    std::cout << "PASS " << test << '\n';
}
