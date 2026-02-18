#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string host;
    uint16_t port = 1883;
    std::string user;
    std::string pass;
    std::string clientId = "opt_local_ha_test";
    std::string deviceId = "localtest";
    bool publishSample = false;
    int timeoutSec = 5;
};

void printUsage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " --host <ip-or-host> [options]\n"
        << "Options:\n"
        << "  --port <num>           MQTT port (default: 1883)\n"
        << "  --user <username>      MQTT username\n"
        << "  --pass <password>      MQTT password\n"
        << "  --client-id <id>       MQTT client id (default: opt_local_ha_test)\n"
        << "  --device-id <id>       Device id for topic paths (default: localtest)\n"
        << "  --timeout <sec>        Socket/connect timeout in seconds (default: 5)\n"
        << "  --publish-sample       Publish sample availability/discovery/state\n"
        << "  --help                 Show this help\n\n"
        << "Examples:\n"
        << "  ./ha --host 192.168.87.28 --port 1883\n"
        << "  ./ha --host 192.168.87.28 --user mqtt_user --pass mqtt_pass --publish-sample\n";
}

bool parseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto requireValue = [&](const char* name, std::string& out) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                return false;
            }
            out = argv[++i];
            return true;
        };

        if (arg == "--host") {
            if (!requireValue("--host", opts.host)) return false;
        } else if (arg == "--port") {
            std::string val;
            if (!requireValue("--port", val)) return false;
            long p = std::strtol(val.c_str(), nullptr, 10);
            if (p < 1 || p > 65535) {
                std::cerr << "Invalid --port: " << val << "\n";
                return false;
            }
            opts.port = static_cast<uint16_t>(p);
        } else if (arg == "--user") {
            if (!requireValue("--user", opts.user)) return false;
        } else if (arg == "--pass") {
            if (!requireValue("--pass", opts.pass)) return false;
        } else if (arg == "--client-id") {
            if (!requireValue("--client-id", opts.clientId)) return false;
        } else if (arg == "--device-id") {
            if (!requireValue("--device-id", opts.deviceId)) return false;
        } else if (arg == "--timeout") {
            std::string val;
            if (!requireValue("--timeout", val)) return false;
            long t = std::strtol(val.c_str(), nullptr, 10);
            if (t < 1 || t > 60) {
                std::cerr << "Invalid --timeout: " << val << "\n";
                return false;
            }
            opts.timeoutSec = static_cast<int>(t);
        } else if (arg == "--publish-sample") {
            opts.publishSample = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return false;
        }
    }

    if (opts.host.empty()) {
        std::cerr << "--host is required\n";
        return false;
    }
    if (!opts.user.empty() && opts.pass.empty()) {
        std::cerr << "--pass is required when --user is provided\n";
        return false;
    }

    return true;
}

bool setSocketTimeout(int fd, int timeoutSec) {
    timeval tv{};
    tv.tv_sec = timeoutSec;
    tv.tv_usec = 0;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

int connectTcp(const std::string& host, uint16_t port, int timeoutSec) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    addrinfo* res = nullptr;
    int gai = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
    if (gai != 0) {
        std::cerr << "DNS/addr resolution failed: " << gai_strerror(gai) << "\n";
        return -1;
    }

    int sock = -1;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;

        int flags = 1;
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags));
        if (!setSocketTimeout(sock, timeoutSec)) {
            close(sock);
            sock = -1;
            continue;
        }

        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) {
            freeaddrinfo(res);
            return sock;
        }

        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);
    std::cerr << "TCP connect failed to " << host << ":" << port
              << " (errno=" << errno << ": " << std::strerror(errno) << ")\n";
    return -1;
}

void appendUtf8(std::vector<uint8_t>& out, const std::string& s) {
    out.push_back(static_cast<uint8_t>((s.size() >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(s.size() & 0xFF));
    out.insert(out.end(), s.begin(), s.end());
}

void appendRemainingLength(std::vector<uint8_t>& out, size_t len) {
    do {
        uint8_t encoded = static_cast<uint8_t>(len % 128);
        len /= 128;
        if (len > 0) encoded |= 0x80;
        out.push_back(encoded);
    } while (len > 0);
}

std::vector<uint8_t> makeConnectPacket(const Options& opts) {
    std::vector<uint8_t> vhPayload;

    appendUtf8(vhPayload, "MQTT");
    vhPayload.push_back(0x04); // MQTT 3.1.1

    uint8_t connectFlags = 0x02; // Clean Session
    if (!opts.user.empty()) connectFlags |= 0x80;
    if (!opts.pass.empty()) connectFlags |= 0x40;
    vhPayload.push_back(connectFlags);

    const uint16_t keepAliveSec = 60;
    vhPayload.push_back(static_cast<uint8_t>((keepAliveSec >> 8) & 0xFF));
    vhPayload.push_back(static_cast<uint8_t>(keepAliveSec & 0xFF));

    appendUtf8(vhPayload, opts.clientId);
    if (!opts.user.empty()) appendUtf8(vhPayload, opts.user);
    if (!opts.pass.empty()) appendUtf8(vhPayload, opts.pass);

    std::vector<uint8_t> packet;
    packet.push_back(0x10); // CONNECT
    appendRemainingLength(packet, vhPayload.size());
    packet.insert(packet.end(), vhPayload.begin(), vhPayload.end());
    return packet;
}

bool writeAll(int fd, const std::vector<uint8_t>& data) {
    size_t written = 0;
    while (written < data.size()) {
        ssize_t n = send(fd, data.data() + written, data.size() - written, 0);
        if (n <= 0) return false;
        written += static_cast<size_t>(n);
    }
    return true;
}

bool readExact(int fd, uint8_t* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, buf + got, len - got, 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

int readConnAckCode(int fd) {
    uint8_t fixed[4] = {0};
    if (!readExact(fd, fixed, sizeof(fixed))) return -1;

    if (fixed[0] != 0x20 || fixed[1] != 0x02) {
        std::cerr << "Unexpected CONNACK packet header: 0x"
                  << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(fixed[0]) << " 0x"
                  << static_cast<int>(fixed[1]) << std::dec << "\n";
        return -1;
    }

    return fixed[3];
}

const char* connAckMeaning(int code) {
    switch (code) {
        case 0: return "Connection Accepted";
        case 1: return "Unacceptable protocol version";
        case 2: return "Identifier rejected";
        case 3: return "Server unavailable";
        case 4: return "Bad username/password";
        case 5: return "Not authorized";
        default: return "Unknown/transport error";
    }
}

std::vector<uint8_t> makePublishPacket(const std::string& topic,
                                       const std::string& payload,
                                       bool retained) {
    std::vector<uint8_t> varPayload;
    appendUtf8(varPayload, topic);
    varPayload.insert(varPayload.end(), payload.begin(), payload.end());

    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(0x30 | (retained ? 0x01 : 0x00))); // QoS0 publish
    appendRemainingLength(packet, varPayload.size());
    packet.insert(packet.end(), varPayload.begin(), varPayload.end());
    return packet;
}

bool publishSampleData(int fd, const Options& opts) {
    const std::string base = "openprinttag/" + opts.deviceId;
    const std::string availabilityTopic = base + "/availability";
    const std::string tagStateTopic = base + "/tag/state";

    const std::string discoveryTopic =
        "homeassistant/sensor/openprinttag_" + opts.deviceId +
        "/spool/config";

    std::ostringstream discovery;
    discovery
        << "{"
        << "\"~\":\"" << base << "\"," 
        << "\"name\":\"Spool\"," 
        << "\"unique_id\":\"openprinttag_" << opts.deviceId << "_spool\"," 
        << "\"stat_t\":\"~/tag/state\"," 
        << "\"val_tpl\":\"{{ 'present' if value_json.present else 'not_present' }}\"," 
        << "\"json_attr_t\":\"~/tag/state\"," 
        << "\"json_attr_tpl\":\"{{ value_json }}\"," 
        << "\"avty_t\":\"~/availability\"," 
        << "\"ic\":\"mdi:printer-3d-nozzle\"," 
        << "\"dev\":{"
        << "\"ids\":[\"openprinttag_" << opts.deviceId << "\"],"
        << "\"name\":\"OpenPrintTag Scanner\"," 
        << "\"mf\":\"OpenPrintTag\"," 
        << "\"sw\":\"local-test\""
        << "}}";

    std::vector<std::vector<uint8_t>> packets;
    packets.push_back(makePublishPacket(availabilityTopic, "online", true));
    packets.push_back(makePublishPacket(discoveryTopic, discovery.str(), true));
    packets.push_back(makePublishPacket(tagStateTopic,
                                        "{\"uid\":\"TEST123\",\"present\":true,"
                                        "\"material_type\":\"PLA\",\"material_name\":\"PLA\","
                                        "\"color\":\"#FFFFFF\",\"manufacturer\":\"\","
                                        "\"remaining_g\":845.0,\"initial_weight_g\":1000.0,"
                                        "\"spoolman_id\":-1,\"blank\":false}",
                                        true));

    for (const auto& pkt : packets) {
        if (!writeAll(fd, pkt)) return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parseArgs(argc, argv, opts)) {
        printUsage(argv[0]);
        return 2;
    }

    int fd = connectTcp(opts.host, opts.port, opts.timeoutSec);
    if (fd < 0) return 1;

    auto connectPacket = makeConnectPacket(opts);
    if (!writeAll(fd, connectPacket)) {
        std::cerr << "Failed to send MQTT CONNECT\n";
        close(fd);
        return 1;
    }

    int code = readConnAckCode(fd);
    if (code != 0) {
        std::cerr << "MQTT CONNACK code " << code << " (" << connAckMeaning(code) << ")\n";
        close(fd);
        return 1;
    }

    std::cout << "MQTT connect OK to " << opts.host << ":" << opts.port << "\n";

    if (opts.publishSample) {
        if (!publishSampleData(fd, opts)) {
            std::cerr << "Connected, but failed publishing sample data\n";
            close(fd);
            return 1;
        }
        std::cout << "Published sample availability/discovery/tag-state for device_id="
                  << opts.deviceId << "\n";
    }

    // DISCONNECT packet
    const uint8_t disconnectPacket[2] = {0xE0, 0x00};
    send(fd, disconnectPacket, sizeof(disconnectPacket), 0);
    close(fd);

    return 0;
}
