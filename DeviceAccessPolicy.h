#pragma once
// Integration credentials are for media controls, never administration/OTA.
bool deviceControlRoute(const char* path, bool get, bool post = false);
