# ESP32 + SHT30 bring-up firmware

This sketch reads real temperature and humidity values from an SHT30 over I2C,
encodes them as EdgeLink binary frames, and streams them over Wi-Fi/TCP to the
C++ gateway.

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
3. Copy `secrets.example.h` to `secrets.h` and enter the local Wi-Fi name
   and password. The real `secrets.h` file is ignored by Git.
4. Upload `esp32_sht30.ino`.
5. Open Serial Monitor at `115200` baud.

Expected output:

```text
Wi-Fi connected, ESP32 IP: 192.168.1.123
Connecting to EdgeLink gateway...connected
HELLO sent, seq=1
SHT30 connected
Temperature: 28.14 C, Humidity: 55.39 %
TELEMETRY sent, seq=2
```

The firmware currently sends HELLO and TELEMETRY frames. Receiving ACKs,
sending heartbeats, and reconnecting after failures are the next milestones.
