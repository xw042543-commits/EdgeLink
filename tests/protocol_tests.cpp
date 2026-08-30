#include "edgelink/protocol.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void check(bool condition, const std::string& name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        std::exit(1);
    }
    std::cout << "PASS: " << name << '\n';
}

edgelink::Message make_message(edgelink::MessageType type, std::uint32_t sequence,
                               const std::string& payload) {
    return {type, sequence, {payload.begin(), payload.end()}};
}

} // namespace

int main() {
    using namespace edgelink;

    const auto source = make_message(MessageType::telemetry, 42, "temperature=24.5");
    const auto frame = encode(source);
    StreamParser parser;
    const auto decoded = parser.push(frame);
    check(decoded.size() == 1, "round trip yields one message");
    check(decoded[0].sequence == 42 && decoded[0].payload_as_string() == "temperature=24.5",
          "round trip preserves content");

    StreamParser split_parser;
    const auto first = split_parser.push(std::span(frame).first(1));
    const auto second = split_parser.push(std::span(frame).subspan(1));
    check(first.empty() && second.size() == 1, "split TCP frame is reassembled");

    const auto heartbeat = encode(make_message(MessageType::heartbeat, 43, ""));
    std::vector<std::uint8_t> joined = frame;
    joined.insert(joined.end(), heartbeat.begin(), heartbeat.end());
    StreamParser joined_parser;
    check(joined_parser.push(joined).size() == 2, "coalesced TCP frames are separated");

    auto damaged = frame;
    damaged.back() ^= 0xFF;
    damaged.insert(damaged.end(), heartbeat.begin(), heartbeat.end());
    StreamParser recovery_parser;
    const auto recovered = recovery_parser.push(damaged);
    check(recovery_parser.rejected_frames() == 1, "CRC corruption is rejected");
    check(recovered.size() == 1 && recovered[0].type == MessageType::heartbeat,
          "parser resynchronizes after corruption");

    const HelloPayload hello{0x0102030405060708ULL, "factory-sensor-01"};
    const auto decoded_hello = decode_hello(encode_hello(hello));
    check(decoded_hello && decoded_hello->session_id == hello.session_id &&
              decoded_hello->device_id == hello.device_id,
          "binary HELLO payload round trips");

    const TelemetryPayload telemetry{1725000000123ULL, 2456, 5832, 3310, 0};
    const auto decoded_telemetry = decode_telemetry(encode_telemetry(telemetry));
    check(decoded_telemetry && decoded_telemetry->timestamp_ms == telemetry.timestamp_ms &&
              decoded_telemetry->temperature_centi_c == telemetry.temperature_centi_c &&
              decoded_telemetry->humidity_centi_pct == telemetry.humidity_centi_pct &&
              decoded_telemetry->voltage_mv == telemetry.voltage_mv,
          "binary telemetry payload round trips");

    std::cout << "All protocol tests passed.\n";
}
