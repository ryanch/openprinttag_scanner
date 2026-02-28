#include "JsonPullHelpers.h"
#include <cstring>

#ifndef NATIVE_TEST
HttpClientStream::HttpClientStream(WiFiClient& client) : m_stream(&client) {}

int HttpClientStream::getch() { return m_stream.getch(); }
size_t HttpClientStream::read(uint8_t* buffer, size_t size) { return m_stream.read(buffer, size); }
int HttpClientStream::putch(int value) { return m_stream.putch(value); }
size_t HttpClientStream::write(const uint8_t* source, size_t size) { return m_stream.write(source, size); }
unsigned long long HttpClientStream::seek(long long position, seek_origin origin) { return m_stream.seek(position, origin); }
stream_caps HttpClientStream::caps() const { return m_stream.caps(); }
#endif

JsonFieldExtractor::JsonFieldExtractor(json_reader& reader) : m_reader(reader) {}

bool JsonFieldExtractor::readStringValue(char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return false;
    out[0] = '\0';

    json_node_type node = m_reader.node_type();
    if (node != json_node_type::value &&
        node != json_node_type::value_part &&
        node != json_node_type::end_value_part) {
        return false;
    }

    size_t written = 0;
    auto append = [&]() {
        const char* value = m_reader.value();
        if (value == nullptr) return;
        while (*value != '\0' && written + 1 < outSize) {
            out[written++] = *value++;
        }
        out[written] = '\0';
    };

    append();
    if (node == json_node_type::value_part) {
        while (m_reader.read()) {
            json_node_type next = m_reader.node_type();
            if (next != json_node_type::value_part &&
                next != json_node_type::end_value_part) {
                return written > 0;
            }
            append();
            if (next == json_node_type::end_value_part) {
                break;
            }
        }
    }
    return written > 0;
}

bool JsonFieldExtractor::findFieldAtCurrentDepth(const char* fieldName) {
    const unsigned depth = m_reader.depth();
    while (m_reader.read()) {
        if (m_reader.node_type() == json_node_type::end_object &&
            m_reader.depth() == depth) {
            return false;
        }
        if (m_reader.node_type() == json_node_type::field &&
            m_reader.depth() == depth &&
            strcmp(m_reader.value(), fieldName) == 0) {
            return m_reader.read();
        }
    }
    return false;
}

bool JsonFieldExtractor::extractFloat(const char* fieldName, float& out) {
    if (!findFieldAtCurrentDepth(fieldName)) return false;
    if (m_reader.node_type() != json_node_type::value) return false;
    if (m_reader.value_type() != json_value_type::real &&
        m_reader.value_type() != json_value_type::integer) {
        return false;
    }
    out = static_cast<float>(m_reader.value_real());
    return true;
}

bool JsonFieldExtractor::extractInt(const char* fieldName, int32_t& out) {
    if (!findFieldAtCurrentDepth(fieldName)) return false;
    if (m_reader.node_type() != json_node_type::value) return false;
    if (m_reader.value_type() != json_value_type::integer &&
        m_reader.value_type() != json_value_type::real) {
        return false;
    }
    out = static_cast<int32_t>(m_reader.value_int());
    return true;
}

bool JsonFieldExtractor::extractString(const char* fieldName, char* out, size_t outSize) {
    if (!findFieldAtCurrentDepth(fieldName)) return false;
    return readStringValue(out, outSize);
}

bool JsonFieldExtractor::extractBool(const char* fieldName, bool& out) {
    if (!findFieldAtCurrentDepth(fieldName)) return false;
    if (m_reader.node_type() != json_node_type::value ||
        m_reader.value_type() != json_value_type::boolean) {
        return false;
    }
    out = m_reader.value_bool();
    return true;
}

bool JsonFieldExtractor::navigateToField(const char* path) {
    if (path == nullptr || *path == '\0') return false;

    char pathCopy[256];
    strncpy(pathCopy, path, sizeof(pathCopy) - 1);
    pathCopy[sizeof(pathCopy) - 1] = '\0';

    char* save = nullptr;
    char* part = strtok_r(pathCopy, ".", &save);
    while (part != nullptr) {
        if (!findFieldAtCurrentDepth(part)) {
            return false;
        }
        char* next = strtok_r(nullptr, ".", &save);
        if (next != nullptr && m_reader.node_type() != json_node_type::object) {
            return false;
        }
        part = next;
    }
    return true;
}

bool JsonFieldExtractor::enterArray(const char* fieldName) {
    if (fieldName == nullptr) {
        return m_reader.read() && m_reader.node_type() == json_node_type::array;
    }
    return findFieldAtCurrentDepth(fieldName) && m_reader.node_type() == json_node_type::array;
}

bool JsonFieldExtractor::nextArrayElement() {
    while (m_reader.read()) {
        if (m_reader.node_type() == json_node_type::end_array) {
            return false;
        }
        if (m_reader.node_type() == json_node_type::object ||
            m_reader.node_type() == json_node_type::array ||
            m_reader.node_type() == json_node_type::value ||
            m_reader.node_type() == json_node_type::value_part ||
            m_reader.node_type() == json_node_type::end_value_part) {
            return true;
        }
    }
    return false;
}

void JsonFieldExtractor::skipValue() {
    if (m_reader.node_type() != json_node_type::object &&
        m_reader.node_type() != json_node_type::array) {
        return;
    }
    const unsigned startDepth = m_reader.depth();
    const json_node_type endNode = m_reader.node_type() == json_node_type::object
        ? json_node_type::end_object
        : json_node_type::end_array;
    while (m_reader.read()) {
        if (m_reader.node_type() == endNode && m_reader.depth() == startDepth) {
            return;
        }
    }
}

void JsonFieldExtractor::reset() {
    // json_reader streams cannot rewind; callers should create a new reader.
}
