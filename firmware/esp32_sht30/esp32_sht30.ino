#include <Wire.h>
#include <Adafruit_SHT31.h>

#include <cstdint>

namespace {

constexpr int kSdaPin = 21;
constexpr int kSclPin = 22;
constexpr std::uint8_t kSht30Address = 0x44;
constexpr unsigned long kSampleIntervalMs = 2000;

Adafruit_SHT31 sensor;

}  // namespace

void setup() {
  Serial.begin(115200);
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
  }

  delay(kSampleIntervalMs);
}
