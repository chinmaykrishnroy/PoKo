#pragma once
#include <WebServer.h>
bool deviceRequestAuthorized(WebServer& server, bool challenge = true);
void installDeviceAuthentication(WebServer& server);
