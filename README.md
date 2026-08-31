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
- Linux `epoll` gateway with non-blocking client sockets
- Per-connection input parsers and queued non-blocking ACK writes
- Inactivity cleanup and graceful `SIGINT`/`SIGTERM` shutdown
- Concurrent load generator with throughput and delivery statistics
- Ubuntu CI with an automated multi-device smoke test

## Architecture

```text
Device simulator / ESP32
          │ TCP binary frames
          ▼
  Linux epoll gateway
  ├── one non-blocking event loop
  ├── parser state per connection
  ├── CRC and payload validation
  ├── queued ACK output per connection
  └── identity and inactivity tracking
```

The original portable gateway remains available for macOS development. The Linux
`epoll_gateway` is the scalable v0.2 implementation. See
[`docs/epoll-design.md`](docs/epoll-design.md) for the event flow and design decisions.

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

Build the Linux-only epoll gateway:

```bash
make epoll
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

## Run the epoll demo on Linux

Terminal 1:

```bash
./build/epoll_gateway 9040
```

Terminal 2:

```bash
./build/device_simulator esp32-sim-001 127.0.0.1 9040 28.5 65.2
```

Start more simulator processes with different device IDs to demonstrate concurrent
connections.

## Run a concurrent load test

The following command starts 100 simulated devices. Each device completes HELLO and
sends 100 acknowledged telemetry messages:

```bash
./build/load_generator 127.0.0.1 9040 100 100
```

The summary reports connected clients, acknowledged telemetry messages, elapsed time,
and application-level messages per second. A smaller automated smoke test is available
on Linux:

```bash
make smoke
```

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

## Status and roadmap

Version 0.2 completes the Linux `epoll` event loop, non-blocking input/output queues,
connection timeouts, concurrent load generation, and Linux CI.

Future milestones:

1. Add a bounded worker queue, structured logging, and richer runtime metrics.
2. Store telemetry in SQLite and expose a small status API.
3. Port the protocol encoder to ESP32 and test with a real temperature sensor.
4. Add sanitizers, packet-loss fault injection, and reproducible benchmark reports.

## Ubuntu validation

![Gateway receiving telemetry on Ubuntu](docs/images/ubuntu-gateway.png)

![Device simulator receiving ACKs on Ubuntu](docs/images/ubuntu-simulator.png)
