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
    int listenCmdSec = 0;
    std::string cmdTplVariant = "topic_uid";
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
        << "  --listen-cmd <sec>     Subscribe and print openprinttag/<device>/cmd/# for N sec\n"
        << "  --cmd-tpl-variant <v>  Command template variant for sample number entity:\n"
        << "                         topic_uid | fixed_spool | self_entity | sibling_from_this\n"
        << "                         (default: topic_uid)\n"
        << "  --help                 Show this help\n\n"
        << "Examples:\n"
        << "  ./ha --host 192.168.87.28 --port 1883\n"
        << "  ./ha --host 192.168.87.28 --user mqtt_user --pass mqtt_pass --publish-sample\n"
        << "  ./ha --host 192.168.87.28 --device-id 199d90 --publish-sample --listen-cmd 60\n";
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
        } else if (arg == "--listen-cmd") {
            std::string val;
            if (!requireValue("--listen-cmd", val)) return false;
            long t = std::strtol(val.c_str(), nullptr, 10);
            if (t < 1 || t > 3600) {
                std::cerr << "Invalid --listen-cmd: " << val << "\n";
                return false;
            }
            opts.listenCmdSec = static_cast<int>(t);
        } else if (arg == "--cmd-tpl-variant") {
            if (!requireValue("--cmd-tpl-variant", opts.cmdTplVariant)) return false;
            if (opts.cmdTplVariant != "topic_uid" &&
                opts.cmdTplVariant != "fixed_spool" &&
                opts.cmdTplVariant != "self_entity" &&
                opts.cmdTplVariant != "sibling_from_this") {
                std::cerr << "Invalid --cmd-tpl-variant: " << opts.cmdTplVariant << "\n";
                return false;
            }
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

std::vector<uint8_t> makeSubscribePacket(uint16_t packetId, const std::string& topicFilter) {
    std::vector<uint8_t> varPayload;
    varPayload.push_back(static_cast<uint8_t>((packetId >> 8) & 0xFF));
    varPayload.push_back(static_cast<uint8_t>(packetId & 0xFF));
    appendUtf8(varPayload, topicFilter);
    varPayload.push_back(0x00); // QoS 0

    std::vector<uint8_t> packet;
    packet.push_back(0x82); // SUBSCRIBE QoS 1
    appendRemainingLength(packet, varPayload.size());
    packet.insert(packet.end(), varPayload.begin(), varPayload.end());
    return packet;
}

bool readRemainingLength(int fd, size_t& outLen) {
    outLen = 0;
    size_t multiplier = 1;
    for (int i = 0; i < 4; ++i) {
        uint8_t b = 0;
        if (!readExact(fd, &b, 1)) return false;
        outLen += (b & 0x7F) * multiplier;
        if ((b & 0x80) == 0) return true;
        multiplier *= 128;
    }
    return false;
}

std::string remainingCmdTpl(const Options& opts) {
    if (opts.cmdTplVariant == "topic_uid") {
        return "{\"remaining_g\": {{ value | float }}}";
    }
    if (opts.cmdTplVariant == "self_entity") {
        return "{\"uid\":{{ state_attr(entity_id, 'uid') | tojson }},\"remaining_g\": {{ value | float }}}";
    }
    if (opts.cmdTplVariant == "sibling_from_this") {
        return "{% set n=this.entity_id.split('.')[1] %}{% set s='sensor.' ~ (n | replace('_set_remaining_filament','_spool')) %}{\"uid\":{{ state_attr(s, 'uid') | tojson }},\"remaining_g\": {{ value | float }}}";
    }
    return "{\"uid\":{{ state_attr('sensor.openprinttag_" + opts.deviceId + "_spool', 'uid') | tojson }},\"remaining_g\": {{ value | float }}}";
}

std::string writeTagCmdTpl(const Options& opts, const std::string& field, const std::string& valueTpl) {
    if (opts.cmdTplVariant == "topic_uid") {
        return "{\"" + field + "\": " + valueTpl + "}";
    }
    if (opts.cmdTplVariant == "self_entity") {
        return "{\"uid\":{{ state_attr(entity_id, 'uid') | tojson }},\"" + field + "\": " + valueTpl + "}";
    }
    if (opts.cmdTplVariant == "sibling_from_this") {
        return "{% set n=this.entity_id.split('.')[1] %}{% set s='sensor.' ~ (n | replace('_set_initial_spool_weight','_spool') | replace('_set_spoolman_id','_spool') | replace('_set_material_type','_spool') | replace('_set_manufacturer','_spool')) %}{\"uid\":{{ state_attr(s, 'uid') | tojson }},\"" + field + "\": " + valueTpl + "}";
    }
    return "{\"uid\":{{ state_attr('sensor.openprinttag_" + opts.deviceId + "_spool', 'uid') | tojson }},\"" + field + "\": " + valueTpl + "}";
}

bool publishSampleData(int fd, const Options& opts) {
    const std::string base = "openprinttag/" + opts.deviceId;
    const std::string availabilityTopic = base + "/availability";
    const std::string tagStateTopic = base + "/tag/state";

    const std::string spoolDiscoveryTopic =
        "homeassistant/sensor/openprinttag_" + opts.deviceId +
        "/spool/config";
    const std::string numberDiscoveryTopic =
        "homeassistant/number/openprinttag_" + opts.deviceId +
        "/set_remaining_weight/config";
    const std::string initialDiscoveryTopic =
        "homeassistant/number/openprinttag_" + opts.deviceId +
        "/set_initial_weight/config";
    const std::string spoolmanDiscoveryTopic =
        "homeassistant/number/openprinttag_" + opts.deviceId +
        "/set_spoolman_id/config";
    const std::string materialDiscoveryTopic =
        "homeassistant/select/openprinttag_" + opts.deviceId +
        "/set_material_type/config";
    const std::string mfrDiscoveryTopic =
        "homeassistant/text/openprinttag_" + opts.deviceId +
        "/set_manufacturer/config";
    const std::string updateCmdTopic =
        (opts.cmdTplVariant == "topic_uid")
            ? ("~/cmd/update_remaining/TEST123")
            : "~/cmd/update_remaining";
    const std::string writeCmdTopic =
        (opts.cmdTplVariant == "topic_uid")
            ? ("~/cmd/write_tag/TEST123")
            : "~/cmd/write_tag";

    std::ostringstream spoolDiscovery;
    spoolDiscovery
        << "{"
        << "\"~\":\"" << base << "\"," 
        << "\"name\":\"Spool\"," 
        << "\"unique_id\":\"openprinttag_" << opts.deviceId << "_spool\"," 
        << "\"obj_id\":\"openprinttag_" << opts.deviceId << "_spool\"," 
        << "\"stat_t\":\"~/tag/state\"," 
        << "\"val_tpl\":\"{{ 'present' if value_json.present else 'not_present' }}\"," 
        << "\"json_attr_t\":\"~/tag/state\"," 
        << "\"json_attr_tpl\":\"{{ value_json }}\"," 
        << "\"avty_t\":\"~/availability\"," 
        << "\"ic\":\"mdi:printer-3d-nozzle\"," 
        << "\"dev\":{"
        << "\"ids\":[\"openprinttag_" << opts.deviceId << "\"],"
        << "\"name\":\"Int Test OpenPrintTag Scanner\"," 
        << "\"mf\":\"OpenPrintTag\"," 
        << "\"sw\":\"local-test\""
        << "}}";

    std::ostringstream numberDiscovery;
    numberDiscovery
        << "{"
        << "\"~\":\"" << base << "\","
        << "\"name\":\"Set Remaining Filament\","
        << "\"unique_id\":\"openprinttag_" << opts.deviceId << "_set_remaining_weight\","
        << "\"obj_id\":\"openprinttag_" << opts.deviceId << "_set_remaining_weight\","
        << "\"stat_t\":\"~/tag/state\","
        << "\"val_tpl\":\"{{ value_json.remaining_g | default(0) }}\","
        << "\"cmd_t\":\"" << updateCmdTopic << "\","
        << "\"cmd_tpl\":\"" << remainingCmdTpl(opts) << "\","
        << "\"avty_t\":\"~/availability\","
        << "\"min\":0.0,\"max\":5000.0,\"step\":1.0,\"mode\":\"box\","
        << "\"unit_of_meas\":\"g\","
        << "\"ic\":\"mdi:weight-gram\","
        << "\"dev\":{\"ids\":[\"openprinttag_" << opts.deviceId << "\"]}"
        << "}";
    std::ostringstream initialDiscovery;
    initialDiscovery
        << "{"
        << "\"~\":\"" << base << "\","
        << "\"name\":\"Set Initial Spool Weight\","
        << "\"unique_id\":\"openprinttag_" << opts.deviceId << "_set_initial_weight\","
        << "\"obj_id\":\"openprinttag_" << opts.deviceId << "_set_initial_weight\","
        << "\"stat_t\":\"~/tag/state\","
        << "\"val_tpl\":\"{{ value_json.initial_weight_g | default(1000) }}\","
        << "\"cmd_t\":\"" << writeCmdTopic << "\","
        << "\"cmd_tpl\":\"" << writeTagCmdTpl(opts, "initial_weight_g", "{{ value | float }}") << "\","
        << "\"avty_t\":\"~/availability\","
        << "\"min\":0.0,\"max\":5000.0,\"step\":1.0,\"mode\":\"box\","
        << "\"unit_of_meas\":\"g\","
        << "\"ic\":\"mdi:scale\","
        << "\"dev\":{\"ids\":[\"openprinttag_" << opts.deviceId << "\"]}"
        << "}";
    std::ostringstream spoolmanDiscovery;
    spoolmanDiscovery
        << "{"
        << "\"~\":\"" << base << "\","
        << "\"name\":\"Set Spoolman ID\","
        << "\"unique_id\":\"openprinttag_" << opts.deviceId << "_set_spoolman_id\","
        << "\"obj_id\":\"openprinttag_" << opts.deviceId << "_set_spoolman_id\","
        << "\"stat_t\":\"~/tag/state\","
        << "\"val_tpl\":\"{{ value_json.spoolman_id | default(-1) }}\","
        << "\"cmd_t\":\"" << writeCmdTopic << "\","
        << "\"cmd_tpl\":\"" << writeTagCmdTpl(opts, "spoolman_id", "{{ value | int }}") << "\","
        << "\"avty_t\":\"~/availability\","
        << "\"min\":-1,\"max\":2000000,\"step\":1,\"mode\":\"box\","
        << "\"ic\":\"mdi:database\","
        << "\"dev\":{\"ids\":[\"openprinttag_" << opts.deviceId << "\"]}"
        << "}";
    std::ostringstream materialDiscovery;
    materialDiscovery
        << "{"
        << "\"~\":\"" << base << "\","
        << "\"name\":\"Set Material Type\","
        << "\"unique_id\":\"openprinttag_" << opts.deviceId << "_set_material_type\","
        << "\"obj_id\":\"openprinttag_" << opts.deviceId << "_set_material_type\","
        << "\"stat_t\":\"~/tag/state\","
        << "\"val_tpl\":\"{{ value_json.material_type | default('PLA') }}\","
        << "\"cmd_t\":\"" << writeCmdTopic << "\","
        << "\"cmd_tpl\":\"" << writeTagCmdTpl(opts, "filament_type", "{{ value | tojson }}") << "\","
        << "\"avty_t\":\"~/availability\","
        << "\"options\":[\"PLA\",\"PETG\",\"ABS\",\"ASA\",\"TPU\",\"PC\",\"Nylon\",\"PVA\",\"HIPS\"],"
        << "\"ic\":\"mdi:printer-3d-nozzle\","
        << "\"dev\":{\"ids\":[\"openprinttag_" << opts.deviceId << "\"]}"
        << "}";
    std::ostringstream mfrDiscovery;
    mfrDiscovery
        << "{"
        << "\"~\":\"" << base << "\","
        << "\"name\":\"Set Manufacturer\","
        << "\"unique_id\":\"openprinttag_" << opts.deviceId << "_set_manufacturer\","
        << "\"obj_id\":\"openprinttag_" << opts.deviceId << "_set_manufacturer\","
        << "\"stat_t\":\"~/tag/state\","
        << "\"val_tpl\":\"{{ value_json.manufacturer | default('') }}\","
        << "\"cmd_t\":\"" << writeCmdTopic << "\","
        << "\"cmd_tpl\":\"" << writeTagCmdTpl(opts, "manufacturer", "{{ value | tojson }}") << "\","
        << "\"avty_t\":\"~/availability\","
        << "\"ic\":\"mdi:factory\","
        << "\"dev\":{\"ids\":[\"openprinttag_" << opts.deviceId << "\"]}"
        << "}";

    std::vector<std::vector<uint8_t>> packets;
    packets.push_back(makePublishPacket(availabilityTopic, "online", true));
    packets.push_back(makePublishPacket(spoolDiscoveryTopic, spoolDiscovery.str(), true));
    packets.push_back(makePublishPacket(numberDiscoveryTopic, numberDiscovery.str(), true));
    packets.push_back(makePublishPacket(initialDiscoveryTopic, initialDiscovery.str(), true));
    packets.push_back(makePublishPacket(spoolmanDiscoveryTopic, spoolmanDiscovery.str(), true));
    packets.push_back(makePublishPacket(materialDiscoveryTopic, materialDiscovery.str(), true));
    packets.push_back(makePublishPacket(mfrDiscoveryTopic, mfrDiscovery.str(), true));
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
    std::cout << "Published number cmd_tpl variant=" << opts.cmdTplVariant << "\n";
    return true;
}

bool listenForCmd(int fd, const Options& opts) {
    const std::string topicFilter = "openprinttag/" + opts.deviceId + "/cmd/#";
    auto subPkt = makeSubscribePacket(1, topicFilter);
    if (!writeAll(fd, subPkt)) {
        std::cerr << "Failed to send SUBSCRIBE\n";
        return false;
    }

    uint8_t fixedHdr = 0;
    if (!readExact(fd, &fixedHdr, 1)) {
        std::cerr << "Failed waiting for SUBACK\n";
        return false;
    }
    size_t remLen = 0;
    if (!readRemainingLength(fd, remLen)) {
        std::cerr << "Failed reading SUBACK remaining length\n";
        return false;
    }
    std::vector<uint8_t> rem(remLen);
    if (remLen > 0 && !readExact(fd, rem.data(), remLen)) {
        std::cerr << "Failed reading SUBACK payload\n";
        return false;
    }

    std::cout << "Listening on " << topicFilter << " for " << opts.listenCmdSec << "s...\n";
    const time_t deadline = time(nullptr) + opts.listenCmdSec;
    while (time(nullptr) < deadline) {
        uint8_t header = 0;
        ssize_t n = recv(fd, &header, 1, MSG_DONTWAIT);
        if (n == 0) break;
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(100000);
                continue;
            }
            std::cerr << "recv error: " << std::strerror(errno) << "\n";
            return false;
        }

        size_t len = 0;
        if (!readRemainingLength(fd, len)) return false;
        std::vector<uint8_t> payload(len);
        if (len > 0 && !readExact(fd, payload.data(), len)) return false;

        const uint8_t packetType = header >> 4;
        if (packetType != 3) continue; // PUBLISH only
        if (len < 2) continue;

        size_t idx = 0;
        size_t topicLen = (static_cast<size_t>(payload[idx]) << 8) | payload[idx + 1];
        idx += 2;
        if (idx + topicLen > payload.size()) continue;
        std::string topic(reinterpret_cast<const char*>(payload.data() + idx), topicLen);
        idx += topicLen;
        if (((header >> 1) & 0x03) > 0) {
            if (idx + 2 > payload.size()) continue;
            idx += 2; // packet id for QoS>0
        }
        std::string body(reinterpret_cast<const char*>(payload.data() + idx), payload.size() - idx);
        std::cout << "CMD topic=" << topic << " payload=" << body << "\n";
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

    if (opts.listenCmdSec > 0) {
        if (!listenForCmd(fd, opts)) {
            close(fd);
            return 1;
        }
    }

    // DISCONNECT packet
    const uint8_t disconnectPacket[2] = {0xE0, 0x00};
    send(fd, disconnectPacket, sizeof(disconnectPacket), 0);
    close(fd);

    return 0;
}
