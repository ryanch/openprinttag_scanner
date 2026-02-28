#ifndef JSON_PULL_HELPERS_H
#define JSON_PULL_HELPERS_H

#include <json.hpp>


#ifndef NATIVE_TEST
#include <WiFiClient.h>
#endif

using namespace io;
using namespace json;

#ifndef NATIVE_TEST
// Stream adapter for HTTPClient WiFiClient
class HttpClientStream : public stream {
private:
    arduino_stream m_stream;

public:
    explicit HttpClientStream(WiFiClient& client);

    int getch() override;
    size_t read(uint8_t* buffer, size_t size) override;
    int putch(int value) override;
    size_t write(const uint8_t* source, size_t size) override;
    unsigned long long seek(long long position, seek_origin origin = seek_origin::start) override;
    stream_caps caps() const override;
};
#endif

class JsonFieldExtractor {
private:
    json_reader& m_reader;

    bool findFieldAtCurrentDepth(const char* fieldName);
    bool readStringValue(char* out, size_t outSize);

public:
    explicit JsonFieldExtractor(json_reader& reader);

    bool extractFloat(const char* fieldName, float& out);
    bool extractInt(const char* fieldName, int32_t& out);
    bool extractString(const char* fieldName, char* out, size_t outSize);
    bool extractBool(const char* fieldName, bool& out);
    bool navigateToField(const char* path);
    bool enterArray(const char* fieldName);
    bool nextArrayElement();
    void skipValue();
    void reset();
};

#endif // JSON_PULL_HELPERS_H
