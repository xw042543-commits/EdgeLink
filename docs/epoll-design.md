# EdgeLink epoll design

## Why epoll exists in this project

The portable v0.1 gateway assigns one thread to each connected device. That is easy to
understand, but every additional connection consumes another thread and stack. The Linux
v0.2 gateway uses one event loop to watch many sockets instead.

`epoll` does not read network data. It only reports which file descriptors are ready.
The gateway still calls `accept`, `recv`, and `send` to perform the actual work.

## Connection state

Each client file descriptor owns three pieces of state:

```text
fd
├── StreamParser       incomplete bytes received from TCP
├── device ID          identity learned from HELLO
└── pending ACK bytes  output not yet accepted by the kernel
```

All three entries are created after `accept` and removed together when the client
disconnects, times out, or encounters an I/O error.

## Input flow

```text
EPOLLIN
  -> recv repeatedly
  -> positive count: push bytes into StreamParser
  -> zero: peer closed the connection
  -> EINTR: retry recv
  -> EAGAIN: the socket is drained; return to epoll
```

TCP is a byte stream. One `recv` can contain part of a frame or several frames. The
per-client `StreamParser` preserves incomplete bytes and returns every complete,
CRC-validated message.

## Output flow

ACK frames are appended to the client's pending output vector. The gateway immediately
tries to flush that vector.

```text
send completes
  -> clear the queue
  -> watch EPOLLIN only

send returns EAGAIN
  -> preserve unsent bytes
  -> also watch EPOLLOUT
  -> flush again when epoll reports writable
```

Watching `EPOLLOUT` permanently would cause a busy loop because sockets are usually
writable. EdgeLink enables it only while a client has queued output and disables it as
soon as the queue becomes empty.

## Reliability behavior

- A 15-second inactivity deadline removes silent clients.
- `EPOLLERR`, `EPOLLHUP`, and `EPOLLRDHUP` clean up failed or closed connections.
- `SIGINT` and `SIGTERM` stop the loop and close every remaining client.
- The load generator verifies that each telemetry sequence receives its matching ACK.
