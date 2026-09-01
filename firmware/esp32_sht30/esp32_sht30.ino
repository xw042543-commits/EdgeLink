#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <WiFi.h>

#include <array>
#include <vector>
#include <cstdint>

#include "secrets.h"

namespace {

constexpr int kSdaPin = 21;
constexpr int kSclPin = 22;
constexpr std::uint8_t kSht30Address = 0x44;
constexpr unsigned long kSampleIntervalMs = 2000;
constexpr char kDeviceId[] = "esp32-real-001";
constexpr std::uint16_t kProtocolMagic = 0x4544;
constexpr std::uint8_t kProtocolVersion = 1;
constexpr std::uint8_t kHelloMessageType = 1;
constexpr std::uint8_t kTelemetryMessageType = 2;
constexpr std::uint8_t kAckMessageType = 4;
constexpr std::size_t kHeaderSize = 14;
constexpr unsigned long kAckTimeoutMs = 2000;
std::uint32_t nextSequence = 1;

Adafruit_SHT31 sensor;

constexpr char kGatewayHost[] = "192.168.100.48";
constexpr std::uint16_t kGatewayPort = 9000;

WiFiClient gatewayClient;

void appendU16(std::vector<std::uint8_t>& output,
               std::uint16_t value) {
  output.push_back(static_cast<std::uint8_t>(value >> 8U));
  output.push_back(static_cast<std::uint8_t>(value));
}

void appendU32(std::vector<std::uint8_t>& output,
               std::uint32_t value) {
  output.push_back(static_cast<std::uint8_t>(value >> 24U));
  output.push_back(static_cast<std::uint8_t>(value >> 16U));
  output.push_back(static_cast<std::uint8_t>(value >> 8U));
  output.push_back(static_cast<std::uint8_t>(value));
}

void appendU64(std::vector<std::uint8_t>& output,
               std::uint64_t value) {
  appendU32(output, static_cast<std::uint32_t>(value >> 32U));
  appendU32(output, static_cast<std::uint32_t>(value));
}

std::uint16_t readU16(const std::uint8_t* data) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(data[0]) << 8U) |
      data[1]);
}

std::uint32_t readU32(const std::uint8_t* data) {
  return (static_cast<std::uint32_t>(data[0]) << 24U) |
         (static_cast<std::uint32_t>(data[1]) << 16U) |
         (static_cast<std::uint32_t>(data[2]) << 8U) |
         static_cast<std::uint32_t>(data[3]);
}

std::uint32_t calculateCrc32(
    const std::vector<std::uint8_t>& data) {
  std::uint32_t crc = 0xFFFFFFFFU;

  for (const std::uint8_t byte : data) {
    crc ^= byte;

    for (int bit = 0; bit < 8; ++bit) {
      if ((crc & 1U) != 0U) {
        crc = (crc >> 1U) ^ 0xEDB88320U;
      } else {
        crc >>= 1U;
      }
    }
  }

  return ~crc;
}

std::vector<std::uint8_t> buildHelloPayload() {
  std::vector<std::uint8_t> payload;

  const std::uint64_t sessionId =
      ESP.getEfuseMac() ^ static_cast<std::uint64_t>(micros());

  appendU64(payload, sessionId);

  payload.insert(
      payload.end(),
      kDeviceId,
      kDeviceId + sizeof(kDeviceId) - 1);

  return payload;
}

std::vector<std::uint8_t> buildTelemetryPayload(
    float temperature,
    float humidity) {
  std::vector<std::uint8_t> payload;

  const std::int16_t temperatureCenti =
      static_cast<std::int16_t>(temperature * 100.0F);
  const std::uint16_t humidityCenti =
      static_cast<std::uint16_t>(humidity * 100.0F);

  appendU64(payload, static_cast<std::uint64_t>(millis()));
  appendU16(
      payload,
      static_cast<std::uint16_t>(temperatureCenti));
  appendU16(payload, humidityCenti);
  appendU16(payload, 0);  // Supply voltage is not measured yet.
  appendU16(payload, 0);  // Sensor status: zero means healthy.

  return payload;
}

std::vector<std::uint8_t> buildFrame(
    std::uint8_t messageType,
    std::uint32_t sequence,
    const std::vector<std::uint8_t>& payload) {
  std::vector<std::uint8_t> checksumInput;

  appendU16(checksumInput, kProtocolMagic);
  checksumInput.push_back(kProtocolVersion);
  checksumInput.push_back(messageType);
  appendU16(
      checksumInput,
      static_cast<std::uint16_t>(payload.size()));
  appendU32(checksumInput, sequence);
  checksumInput.insert(
      checksumInput.end(),
      payload.begin(),
      payload.end());

  const std::uint32_t checksum =
      calculateCrc32(checksumInput);

  std::vector<std::uint8_t> frame;
  frame.reserve(14 + payload.size());

  // 前10字节协议头
  frame.insert(
      frame.end(),
      checksumInput.begin(),
      checksumInput.begin() + 10);

  // 第10至13字节放CRC32
  appendU32(frame, checksum);

  // 第14字节开始放Payload
  frame.insert(
      frame.end(),
      checksumInput.begin() + 10,
      checksumInput.end());

  return frame;
}

bool receiveAck(std::uint32_t expectedSequence) {
  std::array<std::uint8_t, kHeaderSize> frame{};
  gatewayClient.setTimeout(kAckTimeoutMs);

  const std::size_t bytesRead = gatewayClient.readBytes(
      reinterpret_cast<char*>(frame.data()),
      frame.size());

  if (bytesRead != frame.size()) {
    Serial.println("ACK timeout");
    return false;
  }

  const bool validHeader =
      readU16(frame.data()) == kProtocolMagic &&
      frame[2] == kProtocolVersion &&
      frame[3] == kAckMessageType &&
      readU16(frame.data() + 4) == 0;

  if (!validHeader) {
    Serial.println("Invalid ACK header");
    return false;
  }

  const std::vector<std::uint8_t> checksumInput(
      frame.begin(),
      frame.begin() + 10);
  const std::uint32_t receivedChecksum =
      readU32(frame.data() + 10);

  if (calculateCrc32(checksumInput) != receivedChecksum) {
    Serial.println("Invalid ACK CRC");
    return false;
  }

  if (readU32(frame.data() + 6) != expectedSequence) {
    Serial.println("Unexpected ACK sequence");
    return false;
  }

  return true;
}

void sendHello() {
  const std::vector<std::uint8_t> payload =
      buildHelloPayload();

  const std::vector<std::uint8_t> frame =
      buildFrame(
          kHelloMessageType,
          nextSequence,
          payload);

  const std::size_t bytesSent =
      gatewayClient.write(frame.data(), frame.size());

  if (bytesSent != frame.size()) {
    Serial.println("Failed to send HELLO");
    gatewayClient.stop();
    return;
  }

  Serial.print("HELLO sent, seq=");
  Serial.println(nextSequence);

  if (!receiveAck(nextSequence)) {
    Serial.println("HELLO delivery not acknowledged");
    gatewayClient.stop();
    return;
  }

  Serial.print("ACK received, seq=");
  Serial.println(nextSequence);
  ++nextSequence;
}

void sendTelemetry(float temperature, float humidity) {
  if (!gatewayClient.connected()) {
    Serial.println("Cannot send TELEMETRY: gateway disconnected");
    return;
  }

  const std::vector<std::uint8_t> payload =
      buildTelemetryPayload(temperature, humidity);
  const std::vector<std::uint8_t> frame =
      buildFrame(
          kTelemetryMessageType,
          nextSequence,
          payload);

  const std::size_t bytesSent =
      gatewayClient.write(frame.data(), frame.size());

  if (bytesSent != frame.size()) {
    Serial.println("Failed to send TELEMETRY");
    gatewayClient.stop();
    return;
  }

  Serial.print("TELEMETRY sent, seq=");
  Serial.println(nextSequence);

  if (!receiveAck(nextSequence)) {
    Serial.println("TELEMETRY delivery not acknowledged");
    gatewayClient.stop();
    return;
  }

  Serial.print("ACK received, seq=");
  Serial.println(nextSequence);
  ++nextSequence;
}

void connectGateway() {
  Serial.print("Connecting to EdgeLink gateway...");

  if (gatewayClient.connect(kGatewayHost, kGatewayPort)) {
    Serial.println("connected");
    sendHello();
  } else {
    Serial.println("failed");
  }
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(kWifiSsid, kWifiPassword);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }

  Serial.println();
  Serial.print("Wi-Fi connected, ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  connectWifi();
  connectGateway();

  Wire.begin(kSdaPin, kSclPin);

  if (!sensor.begin(kSht30Address)) {
    Serial.println("SHT30 not found");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("SHT30 connected");
}

void loop() {
  const float temperature = sensor.readTemperature();
  const float humidity = sensor.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Failed to read SHT30");
  } else {
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.print(" C, Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");

    sendTelemetry(temperature, humidity);
  }

  delay(kSampleIntervalMs);
}
