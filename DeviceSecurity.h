#pragma once
#include <WebServer.h>
bool deviceRequestAuthorized(WebServer& server, bool challenge = true);
void installDeviceAuthentication(WebServer& server);
// Physical Settings recovery only. Caller displays the generated password.
bool replaceDevicePassword(const String& password);
