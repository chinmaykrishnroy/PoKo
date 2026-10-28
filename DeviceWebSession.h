#pragma once
#include <WebServer.h>
bool deviceBrowserRequest(WebServer& server);
bool deviceBrowserSessionAuthorized(WebServer& server);
void clearDeviceBrowserSessions();
void installDeviceBrowserSessions(WebServer& server);
