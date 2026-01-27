# Orion-LD Implementation Details

This document describes the internal implementation of the Orion-LD Context Broker, including architecture, data flow, synchronization mechanisms, and design decisions.

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [HTTP Layer (libmicrohttpd)](#http-layer-libmicrohttpd)
4. [Request Processing Pipeline](#request-processing-pipeline)
5. [JSON-LD Context Management](#json-ld-context-management)
6. [Database Layer](#database-layer)
7. [Caching Systems](#caching-systems)
8. [Notification System](#notification-system)
9. [Distributed Operations](#distributed-operations)
10. [Memory Management](#memory-management)
11. [Configuration Options](#configuration-options)
12. [Source Code Structure](#source-code-structure)

---

## Overview

Orion-LD is an NGSI-LD Context Broker implementing the ETSI NGSI-LD API specification. The design prioritizes:

1. **High Performance** - Thread pool with epoll, efficient JSON parsing (kjson library)
2. **Standards Compliance** - Full NGSI-LD API with JSON-LD context expansion/compaction
3. **Scalability** - Connection pooling, caching, distributed operations
4. **Flexibility** - Support for both NGSI-LD and legacy NGSIv2 APIs

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              CLIENT APPLICATIONS                                 │
│                                                                                 │
│   ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│   │   REST API   │  │   IoT Agent  │  │  Dashboard   │  │ Other Broker │       │
│   └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘       │
│          │                 │                 │                 │                │
│          └─────────────────┴────────┬────────┴─────────────────┘                │
│                                     │                                           │
│                            HTTP/HTTPS Requests                                  │
│                                     │                                           │
├─────────────────────────────────────┼───────────────────────────────────────────┤
│                              ORION-LD BROKER                                    │
│                                     │                                           │
│   ┌─────────────────────────────────▼─────────────────────────────────────┐    │
│   │                         libmicrohttpd                                  │    │
│   │                                                                        │    │
│   │   ┌────────────────────────────────────────────────────────────────┐  │    │
│   │   │                    Thread Pool (epoll)                         │  │    │
│   │   │   [Thread 0] [Thread 1] [Thread 2] ... [Thread N-1]           │  │    │
│   │   └────────────────────────────────────────────────────────────────┘  │    │
│   └─────────────────────────────────┬─────────────────────────────────────┘    │
│                                     │                                           │
│   ┌─────────────────────────────────▼─────────────────────────────────────┐    │
│   │                      Request Processing                                │    │
│   │                                                                        │    │
│   │   ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐             │    │
│   │   │  Parse   │─▶│  Check   │─▶│ Service  │─▶│ Response │             │    │
│   │   │ Headers  │  │ Payload  │  │ Routine  │  │  Build   │             │    │
│   │   └──────────┘  └──────────┘  └──────────┘  └──────────┘             │    │
│   └─────────────────────────────────┬─────────────────────────────────────┘    │
│                                     │                                           │
│   ┌─────────────────────────────────▼─────────────────────────────────────┐    │
│   │                         Core Services                                  │    │
│   │                                                                        │    │
│   │   ┌──────────────┐  ┌──────────────┐  ┌──────────────────────────┐   │    │
│   │   │   Context    │  │ Subscription │  │     Registration         │   │    │
│   │   │    Cache     │  │    Cache     │  │       Cache              │   │    │
│   │   └──────────────┘  └──────────────┘  └──────────────────────────┘   │    │
│   │                                                                        │    │
│   │   ┌──────────────┐  ┌──────────────┐  ┌──────────────────────────┐   │    │
│   │   │ Notification │  │  Distributed │  │      Temporal            │   │    │
│   │   │   Sender     │  │  Operations  │  │   (TRoE/Postgres)        │   │    │
│   │   └──────────────┘  └──────────────┘  └──────────────────────────┘   │    │
│   └─────────────────────────────────┬─────────────────────────────────────┘    │
│                                     │                                           │
└─────────────────────────────────────┼───────────────────────────────────────────┘
                                      │
                    ┌─────────────────┼─────────────────┐
                    │                 │                 │
                    ▼                 ▼                 ▼
            ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
            │   MongoDB    │  │  PostgreSQL  │  │   External   │
            │  (Entities)  │  │    (TRoE)    │  │   Brokers    │
            └──────────────┘  └──────────────┘  └──────────────┘
```

---

## Architecture

### Threading Model

Orion-LD supports two threading models, controlled by the `-reqPoolSize` option:

#### 1. Thread Pool Mode (Recommended)

When `reqPoolSize > 0`, libmicrohttpd uses:
- `MHD_USE_SELECT_INTERNALLY | MHD_USE_EPOLL`
- A fixed thread pool handles all incoming connections
- Connections are multiplexed using epoll for efficiency

```c
// src/lib/orionld/mhd/mhdStart.cpp:90
serverMode = (mhdPoolSize > 0)
    ? MHD_USE_SELECT_INTERNALLY | MHD_USE_EPOLL
    : MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD;
```

**Auto-configuration (default):** When `reqPoolSize = -1`, the broker automatically calculates:
```c
reqPoolSize = CPU_cores * 2
```

#### 2. Thread-Per-Connection Mode

When `reqPoolSize = 0`:
- Each incoming connection spawns a new thread
- Higher overhead for connection establishment
- May be useful for long-running requests

### Request State Management

Each request has thread-local state managed in `OrionldState`:

```c
// Thread-local state (src/lib/orionld/common/orionldState.h)
typedef struct OrionldState
{
    // Connection info
    MHD_Connection*    mhdConnection;
    char*              requestPayload;

    // JSON handling
    Kjson              kjson;           // JSON parser instance
    KjNode*            requestTree;     // Parsed request body
    KjNode*            responseTree;    // Response to be sent

    // Context handling
    OrionldContext*    contextP;        // Active JSON-LD context

    // Database
    mongoc_client_t*   mongocClient;    // MongoDB connection

    // Memory management
    KAlloc             kalloc;          // Fast allocator for request lifetime

    // ... additional fields
} OrionldState;
```

---

## HTTP Layer (libmicrohttpd)

### Initialization

libmicrohttpd is started with these key options:

| Option | Description |
|--------|-------------|
| `MHD_OPTION_THREAD_POOL_SIZE` | Number of worker threads |
| `MHD_OPTION_CONNECTION_MEMORY_LIMIT` | Max memory per connection |
| `MHD_OPTION_CONNECTION_LIMIT` | Max concurrent connections |
| `MHD_OPTION_CONNECTION_TIMEOUT` | Idle connection timeout |

### Request Flow

```
MHD receives request
        │
        ▼
mhdRequest() ─────────────────────────── First call: Initialize connection
        │                                 Subsequent calls: Accumulate body
        │
        ▼
mhdConnectionInit() ───────────────────── Parse URL, method, headers
        │
        ▼
mhdConnectionPayloadRead() ────────────── Buffer incoming payload data
        │
        ▼
mhdConnectionTreat() ──────────────────── Route to service routine
        │
        ▼
Service Routine ───────────────────────── Process request, build response
        │
        ▼
mhdRequestEnded() ─────────────────────── Cleanup, free resources
```

---

## Request Processing Pipeline

### 1. URL Parsing and Service Routing

Each NGSI-LD endpoint maps to a service routine:

```c
// Example service definitions (orionldRestServices.h)
{ POST, "/ngsi-ld/v1/entities",                orionldPostEntities     }
{ GET,  "/ngsi-ld/v1/entities",                orionldGetEntities      }
{ GET,  "/ngsi-ld/v1/entities/{entityId}",     orionldGetEntity        }
{ POST, "/ngsi-ld/v1/subscriptions",           orionldPostSubscriptions}
```

### 2. Payload Parsing

The kjson library parses JSON payloads into a tree structure:

```c
// Parse incoming JSON
orionldState.requestTree = kjParse(orionldState.kjson, payload);

// Tree structure example for: {"id": "urn:x", "type": "T"}
//
//   KjObject
//     ├── KjString "id" -> "urn:x"
//     └── KjString "type" -> "T"
```

### 3. Payload Validation

The `payloadCheck` module validates NGSI-LD payloads:
- Entity structure validation
- Attribute type checking
- URI format verification
- JSON-LD context application

### 4. Service Execution

Service routines follow a standard pattern:

```c
bool orionldPostEntities(void)
{
    // 1. Parse and validate payload
    if (pCheckEntity(orionldState.requestTree, ...) == false)
        return true;  // Error already set

    // 2. Expand attribute names using @context
    orionldContextExpand(...);

    // 3. Database operation
    dbEntityCreate(...);

    // 4. Build response
    orionldState.httpStatusCode = 201;

    return true;
}
```

---

## JSON-LD Context Management

### Context Cache

JSON-LD contexts are cached to avoid repeated downloads:

```
┌─────────────────────────────────────────────────────────────────┐
│                      Context Cache                               │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  URL                              │  OrionldContext*      │  │
│  ├───────────────────────────────────┼───────────────────────┤  │
│  │  https://uri.etsi.org/ngsi-ld/v1  │  [Core Context]       │  │
│  │  https://example.org/mycontext    │  [Cached Context]     │  │
│  │  ...                              │  ...                  │  │
│  └───────────────────────────────────┴───────────────────────┘  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Context Operations

| Operation | Description |
|-----------|-------------|
| Expansion | Short names → Full URIs (e.g., "temperature" → "https://example.org/temperature") |
| Compaction | Full URIs → Short names (response formatting) |
| Merging | Combining multiple contexts |

---

## Database Layer

### MongoDB Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    MongoDB Connection Pool                       │
│                                                                 │
│  ┌─────────────┐  ┌─────────────┐       ┌─────────────┐        │
│  │  mongoc     │  │  mongoc     │  ...  │  mongoc     │        │
│  │  client 0   │  │  client 1   │       │  client N   │        │
│  └─────────────┘  └─────────────┘       └─────────────┘        │
│        │                │                     │                 │
│        └────────────────┴──────────┬──────────┘                 │
│                                    │                            │
│                           mongoc_pool                           │
└────────────────────────────────────┼────────────────────────────┘
                                     │
                                     ▼
                              ┌──────────────┐
                              │   MongoDB    │
                              │   Server     │
                              └──────────────┘
```

### Collections

| Collection | Purpose |
|------------|---------|
| `entities` | NGSI-LD entities (current state) |
| `csubs` | Subscriptions |
| `registrations` | Context source registrations |

### Temporal Representation (TRoE)

For temporal queries, Orion-LD uses PostgreSQL:

```
┌─────────────────────────────────────────────────────────────────┐
│                    PostgreSQL (TRoE)                            │
│                                                                 │
│  ┌─────────────────┐  ┌─────────────────┐                      │
│  │    entities     │  │   attributes    │                      │
│  │  (entity info)  │  │ (attr history)  │                      │
│  └─────────────────┘  └─────────────────┘                      │
│                                                                 │
│  ┌─────────────────┐                                           │
│  │   sub_attrs     │                                           │
│  │ (sub-attr hist) │                                           │
│  └─────────────────┘                                           │
└─────────────────────────────────────────────────────────────────┘
```

---

## Caching Systems

### Subscription Cache

Active subscriptions are cached in memory for fast notification triggering:

```c
typedef struct CachedSubscription
{
    char*            subscriptionId;
    char**           entityTypes;        // Trigger types
    char**           attributes;         // Watched attributes
    char*            notificationUrl;    // Where to send notifications
    // ...
} CachedSubscription;
```

**Cache refresh:** A background thread periodically syncs with MongoDB.

### Registration Cache

Context source registrations are cached for distributed query routing:

```c
typedef struct RegCacheItem
{
    char*            registrationId;
    char*            contextSourceUrl;
    char**           entityTypes;
    char**           attributes;
    // ...
} RegCacheItem;
```

---

## Notification System

### Notification Modes

| Mode | Description |
|------|-------------|
| `transient` | Synchronous, inline notification |
| `threadpool` | Asynchronous via thread pool |

### Notification Flow

```
Entity Created/Modified
        │
        ▼
Check Subscription Cache ────────────────── Match entity against subscriptions
        │
        ▼
Build Notification Payload ──────────────── Format according to subscription
        │
        ▼
Queue/Send Notification ─────────────────── HTTP POST to endpoint
        │
        ├──── Success: Update lastSuccess
        │
        └──── Failure: Update lastFailure, retry logic
```

### MQTT Support

Notifications can also be sent via MQTT when configured.

---

## Distributed Operations

### Forwarding

When registrations exist, queries may be forwarded:

```
Client Request
      │
      ▼
Check Registration Cache
      │
      ├──── Local only: Query MongoDB
      │
      └──── Distributed: Query MongoDB + Forward to registered sources
                              │
                              ▼
                     Merge Results
                              │
                              ▼
                     Return to Client
```

---

## Memory Management

### KAlloc (Fast Allocator)

Each request uses a dedicated allocator that:
- Pre-allocates memory buffers
- Avoids malloc/free overhead during request processing
- Bulk-frees all memory when request completes

```c
// Allocate from request buffer
char* str = kaAlloc(&orionldState.kalloc, 256);

// At request end, all memory freed at once
kaBufferReset(&orionldState.kalloc, KFALSE);
```

---

## Configuration Options

Key startup options in `src/app/orionld/orionld.cpp`:

| Option | Default | Description |
|--------|---------|-------------|
| `-port` | 1026 | HTTP listen port |
| `-reqPoolSize` | -1 (auto) | Thread pool size (-1=auto, 0=thread-per-connection) |
| `-dbhost` | localhost | MongoDB host |
| `-dbpool` | 10 | MongoDB connection pool size |
| `-subCacheInterval` | 60 | Subscription cache refresh interval (seconds) |
| `-notificationMode` | transient | Notification mode (transient/threadpool) |
| `-troe` | false | Enable Temporal Representation |
| `-distributed` | false | Enable distributed operations |

---

## Source Code Structure

```
src/
├── app/
│   └── orionld/
│       └── orionld.cpp              # Main entry point, argument parsing
│
└── lib/
    └── orionld/
        ├── common/                  # Shared utilities, state management
        │   ├── orionldState.h       # Thread-local request state
        │   └── tenantList.h         # Multi-tenancy support
        │
        ├── mhd/                     # libmicrohttpd integration
        │   ├── mhdStart.cpp         # HTTP server startup
        │   └── mhdRequest.cpp       # Request handling
        │
        ├── service/                 # Service initialization
        │   └── orionldServiceInit.cpp
        │
        ├── serviceRoutines/         # API endpoint implementations
        │   ├── orionldGetEntities.cpp
        │   ├── orionldPostEntities.cpp
        │   └── ...
        │
        ├── payloadCheck/            # Request payload validation
        │   ├── pCheckEntity.cpp
        │   └── ...
        │
        ├── context/                 # JSON-LD context handling
        │   └── orionldContextFromUrl.cpp
        │
        ├── contextCache/            # Context caching
        │
        ├── db/                      # Database abstraction
        │   └── dbInit.cpp
        │
        ├── mongoc/                  # MongoDB C driver interface
        │   └── mongocInit.cpp
        │
        ├── subCache/                # Subscription caching
        │
        ├── regCache/                # Registration caching
        │
        ├── notifications/           # Notification handling
        │
        ├── distOp/                  # Distributed operations
        │
        ├── troe/                    # Temporal Representation (PostgreSQL)
        │
        └── types/                   # Data type definitions
```

### External Dependencies

| Library | Purpose |
|---------|---------|
| libmicrohttpd | HTTP server |
| mongoc | MongoDB C driver |
| kjson | Fast JSON parsing |
| kalloc | Memory allocation |
| ktrace | Logging/tracing |
| libpq | PostgreSQL client (for TRoE) |
| libcurl | HTTP client (notifications, forwarding) |
| Paho MQTT | MQTT client |

---

## References

- [ETSI NGSI-LD Specification](https://www.etsi.org/deliver/etsi_gs/CIM/001_099/009/)
- [libmicrohttpd Documentation](https://www.gnu.org/software/libmicrohttpd/)
- [MongoDB C Driver](https://mongoc.org/)
- [JSON-LD Specification](https://www.w3.org/TR/json-ld11/)
