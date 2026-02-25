# Multithreaded Group Chat — C Socket Programming
## Overview

This project implements a multi-client group chat server and client entirely in C. The server manages multiple simultaneous connections using a thread pool, dispatching each connected client to a worker thread that handles bidirectional, real-time message broadcasting. The client uses poll() to asynchronously monitor both keyboard input and incoming socket data without blocking.

---

## Features

- **Custom Thread Pool** — A fixed pool of POSIX threads (`pthread`) manages all client connections, avoiding the overhead of spawning a new thread per connection.
- **FIFO Client Queue** — Incoming clients are queued in a linked-list queue and dispatched to the next available worker thread.
- **Real-Time Message Broadcasting** — When a client sends a message, the server broadcasts it to every other active client simultaneously.
- **Asynchronous Client I/O** — The client uses `poll()` to multiplex stdin and the socket, so it can send *and* receive messages independently without blocking.
- **Graceful Shutdown** — The server catches `SIGINT` (Ctrl+C) and safely drains the thread pool, closes all file descriptors, and frees all memory before exiting.
- **Buffer Overflow Protection** — All string reads enforce strict bounds checking to prevent segfaults and buffer overflows.
- **Named Clients** — Each client registers a username upon connecting, which is prepended to all broadcasted messages.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        SERVER                               │
│                                                             │
│   main()                                                    │
│   ├── accept() loop  ─────────► add_client()               │
│   │                                  │                      │
│   │                             FIFO Client Queue           │
│   │                                  │                      │
│   └── ThreadPool (8 threads)         │                      │
│        ├── worker_function() ◄───────┘                      │
│        │    ├── serve_client()                              │
│        │    │    ├── recvMesg()   ← blocks on socket I/O   │
│        │    │    └── broadcast()  → writes to all others   │
│        │    └── (loop back to queue)                        │
│        └── ... (up to 8 concurrent workers)                 │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                        CLIENT                               │
│                                                             │
│   main()                                                    │
│   ├── connect() to server                                   │
│   ├── send username                                         │
│   └── poll() loop                                           │
│        ├── fds[0] = stdin   → read & write to server       │
│        └── fds[1] = socket  → read & print to terminal     │
└─────────────────────────────────────────────────────────────┘
```

### Key Data Structures

| Structure | Description |
|---|---|
| `Client` | Linked-list node holding a client's socket fd, address info, and username |
| `ThreadPool` | Manages worker threads, the pending-client queue, the active-client list, and all associated mutexes/condition variables |

### Synchronization Primitives

| Primitive | Purpose |
|---|---|
| `q_mutex` + `q_cond` | Guards the FIFO pending-client queue; workers sleep here when idle |
| `active_mutex` + `active_cond` | Guards the active-client list used for broadcasting; also signals the pool on shutdown |

---

## File Structure

```
├── GroupChatServer.c   # Server: thread pool, socket setup, broadcasting
├── GroupChatClient.c   # Client: async I/O with poll(), socket connection
├── Makefile            # Build system with run/clean/test helpers
└── README.md
```

---

## 🔧 Build & Run

### Prerequisites
- GCC with POSIX thread support (`-pthread`)
- Linux / macOS (or WSL on Windows)

### Build

```bash
# Build both server and client
make

# Or build individually
make server
make client
```

### Run

**Terminal 1 — Start the server:**
```bash
./server 50000
```

**Terminal 2 — Connect as Alice:**
```bash
./client Alice localhost 50000
```

**Terminal 3 — Connect as Bob:**
```bash
./client Bob localhost 50000
```

**Terminal 4 — Connect as Charlie:**
```bash
./client Charlie localhost 50000
```

Now any message typed by Alice will appear in Bob's and Charlie's terminals, and vice versa.

### Stop the Server
Press `Ctrl+C` — the server will gracefully shut down all active connections and free all resources.

---

## Usage Reference

```
Server:   ./server <port>
Client:   ./client <name> <server_hostname> <port>
```

| Argument | Description |
|---|---|
| `<port>` | Port number for the server to listen on (e.g. `50000`) |
| `<name>` | Display name for the client (max 19 characters) |
| `<server_hostname>` | Hostname or IP of the server (e.g. `localhost` or `192.168.1.5`) |

---

## Makefile Targets

```bash
make              # Build both server and client
make server       # Build server only
make client       # Build client only
make run-server   # Build and run server on default port 8080
make run-client   # Build and run client connecting to localhost:8080
make clean        # Remove compiled binaries
make test         # Compile and print test instructions
make help         # Show all available targets
```

Custom port:
```bash
make run-server PORT=9000
make run-client PORT=9000
```

---

## Technical Deep-Dive

### Thread Pool Design
The server initializes a pool of **8 worker threads** at startup (configurable via `MAX_NUM_THREADS`). Rather than spawning a new OS thread for each connection — which is expensive — incoming clients are enqueued into a **FIFO linked list**. Idle worker threads sleep on a **condition variable** (`q_cond`) and are woken by a `pthread_cond_signal()` call whenever a new client is added.

### Broadcasting
When a client sends a message, `serve_client()` locks the `active_mutex`, snapshots the file descriptors of all other connected clients, unlocks the mutex, and then writes the message to each fd. The lock is held only during the snapshot — not during the writes — to minimize contention.

### Async Client I/O
Rather than using two threads (one for send, one for receive), the client uses a single `poll()` call over two file descriptors: `stdin` (fd `0`) and the socket. This avoids thread management entirely on the client side while remaining fully non-blocking.

### Graceful Shutdown
On `SIGINT`, a `volatile sig_atomic_t keep_running` flag is cleared. The main accept loop exits, `destroy_thread_pool()` signals all idle workers via `pthread_cond_broadcast()`, shuts down all active client sockets to unblock any blocking `read()` calls, and then joins all threads before freeing memory.

---

## Technologies & Concepts

`C` · `POSIX Sockets` · `TCP/IP` · `pthreads` · `Mutexes` · `Condition Variables` · `poll()` · `Signal Handling` · `Thread Pool Pattern` · `Linked Lists` · `Network Programming`

---
