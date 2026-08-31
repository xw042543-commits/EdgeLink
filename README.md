# EdgeLink

EdgeLink is a C++20 device communication gateway for Linux. It accepts telemetry from
embedded devices over TCP, validates a binary protocol, tracks connected devices, and
recovers the byte stream after malformed or corrupted frames.

The first milestone uses a device simulator. A real ESP32 can later implement the same
wire protocol without changing the gateway architecture.

## Current features

- Versioned binary framing protocol with a 4 KiB payload limit
- CRC-32 corruption detection
- Incremental parsing for TCP fragmentation and coalescing
- Stream resynchronization after invalid data
- Concurrent device sessions
- Thread-safe device registry
- HELLO, TELEMETRY, HEARTBEAT, and ACK messages
- Protocol tests and a deterministic ESP32-style simulator
- ACK-based telemetry delivery confirmation
- Periodic heartbeats with acknowledgment checking
- Inactive connection timeouts and clean signal handling
- Initial connection retries and automatic reconnection after connection loss

## Architecture

```text
Device simulator / ESP32
          │ TCP binary frames
          ▼
  EdgeLink gateway
  ├── stream parser
  ├── CRC validation
  ├── session handling
  └── device registry
```

## Build

With CMake on Linux or macOS:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The dependency-free fallback Makefile is useful on machines without CMake:

```bash
make
make test
```

### Verified environment

EdgeLink has been compiled and tested on:

- Ubuntu Linux ARM64 (`aarch64`) with GCC and GNU Make
- macOS ARM64 with Apple Clang

Validation includes:

- Clean compilation with C++20 warnings enabled
- All protocol tests passing
- End-to-end gateway and simulator communication
- Telemetry acknowledgments and periodic heartbeat acknowledgments

## Run the demo

Terminal 1:

```bash
./build/edgelink_gateway 9000
```

Terminal 2:

```bash
./build/device_simulator esp32-sim-001 127.0.0.1 9000
```

Start more simulator processes with different device IDs to demonstrate concurrent
connections.

## Wire format

All integer fields use network byte order.

| Field | Bytes | Description |
|---|---:|---|
| Magic | 2 | ASCII `ED` |
| Version | 1 | Protocol version (`1`) |
| Type | 1 | Message type |
| Payload length | 2 | 0 to 4096 |
| Sequence | 4 | Per-device sequence number |
| CRC-32 | 4 | Header-without-CRC plus payload |
| Payload | variable | Message content |

## Roadmap

1. Replace thread-per-connection sessions with Linux `epoll` and bounded worker queues.
2. Add a load generator, structured logging, runtime metrics, and performance benchmarks.
3. Store telemetry in SQLite and expose a small status API.
4. Port the protocol encoder to ESP32 and test with real temperature and humidity sensors.
5. Add Linux CI, sanitizers, and packet-loss fault injection.

## Ubuntu validation

![Gateway receiving telemetry on Ubuntu](docs/images/ubuntu-gateway.png)

![Device simulator receiving ACKs on Ubuntu](docs/images/ubuntu-simulator.png)
