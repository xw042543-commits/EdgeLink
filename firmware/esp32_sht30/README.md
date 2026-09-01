# ESP32 + SHT30 bring-up firmware

This sketch is the first hardware milestone for EdgeLink. It reads real
temperature and humidity values from an SHT30 over I2C and prints them to the
USB serial monitor.

## Hardware

- ESP32-WROOM-32 30-pin development board
- SHT30/SHT3x I2C temperature and humidity module
- Four female-to-female jumper wires

## Wiring

Disconnect USB power before changing any wire.

| SHT30 | ESP32 |
| --- | --- |
| `VIN` | `3V3` |
| `GND` | `GND` |
| `SCL/T` | `D22` |
| `SDA/RH` | `D21` |

Use the ESP32 `3V3` pin, not its `VIN`/5V pin.

## Arduino setup

1. Select `ESP32 Dev Module` and the ESP32 serial port.
2. Install `Adafruit SHT31 Library by Adafruit` and its dependencies.
3. Upload `esp32_sht30.ino`.
4. Open Serial Monitor at `115200` baud.

Expected output:

```text
SHT30 connected
Temperature: 28.14 C, Humidity: 55.39 %
```

This firmware does not send data to the EdgeLink gateway yet. The next
milestone adds Wi-Fi, TCP, and the EdgeLink wire protocol.
