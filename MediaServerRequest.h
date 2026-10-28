#pragma once
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

// Own clients before HTTPClient so it is destroyed first. Every media request
// shares the same authentication/TLS policy; never follow redirects with a token.
class MediaServerRequest {
    NetworkClient _plain;
    NetworkClientSecure _secure;
    String _ca;
    HTTPClient _http;
public:
    bool begin(String url);
    int GET();
    void end();
    void setConnectTimeout(int32_t ms);
    void setTimeout(uint16_t ms);
    Stream& getStream();
    NetworkClient* getStreamPtr();
    int getSize();
    bool connected();
};
