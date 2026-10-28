#pragma once
#include <WebServer.h>
bool deviceRequestAuthorized(WebServer& server, bool challenge = true);
void installDeviceAuthentication(WebServer& server);
// New passwords are exactly eight printable non-space ASCII characters.
// Existing longer passwords remain valid until explicitly replaced.
bool replaceDevicePassword(const String& password);
