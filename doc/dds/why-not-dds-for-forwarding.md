# Why DDS is the wrong transport for NGSI-LD forwarding

## TL;DR

DDS is built for **typed pub/sub of continuously-flowing telemetry** between
peers that share a schema known at compile time. NGSI-LD forwarding is
**RPC over polymorphic JSON-LD** between a small set of known broker peers.
They are different problem classes. Using DDS as the transport for the
second one would force us to either (a) build hundreds of IDL types just
for the broker's HTTP surface, or (b) collapse everything to opaque strings
— at which point we're sending HTTP-as-bytes over DDS while paying for all
of DDS's machinery (discovery, QoS, type system, CDR) that we no longer
use.

A purpose-built binary IPC plugin — a tiny framed-message protocol over
TCP — does the same job with one-tenth the complexity, no external
dependency, and lower latency.

---

## 1. What DDS actually optimises for

DDS — the OMG Data Distribution Service standard — is designed for one
specific shape of system:

- **Many publishers, many subscribers**, often hundreds, dynamically
  joining and leaving.
- **Typed samples** flowing continuously: position updates, sensor
  readings, control commands.
- A **single, statically-defined schema per topic** (IDL types compiled
  into every participant ahead of time).
- **Rich Quality-of-Service knobs**: durability, reliability, history
  depth, deadline, latency budget, ownership, lifespan, partitions, …
- **Decentralised discovery**: participants find each other over
  multicast or via a discovery server; no central directory.

This is why DDS dominates in robotics, defence, avionics, telemetry,
industrial control — domains where the data model is *finite and known*,
and the wire is *always on*.

NGSI-LD forwarding is none of these things.

---

## 2. Why NGSI-LD forwarding doesn't fit the DDS model

### 2.1. The data model is polymorphic, by design

NGSI-LD attributes carry arbitrary JSON-LD: an attribute's `value` can be
a string today, an object tomorrow, an array next week, and the entity
type is part of the *content*, not the *channel*. An attribute can
sprout new sub-properties (`observedAt`, `unitCode`, `datasetId`, custom
metadata) without a schema change. Two entities of the same type can
expose entirely different attribute sets.

DDS's type system is the opposite of this. **Every topic has exactly one
IDL type, compiled into every participant.** To send a `PATCH attribute`
over DDS you'd have to either:

- Enumerate every possible attribute shape as a distinct IDL type and a
  distinct topic — combinatorially impossible, and impossible to evolve
  without recompiling every broker in the federation; or
- Define one IDL type with a single `string payload` field and put a
  JSON blob inside it — at which point DDS is doing nothing for you that
  TCP wouldn't, while still demanding its full setup cost.

Neither is viable for a forwarding layer that must handle whatever
NGSI-LD payload a client sends.

### 2.2. NGSI-LD's wire is HTTP — and we use that fact heavily

NGSI-LD doesn't just send JSON. It uses:

- **Different verbs** for different intents (`POST` create, `GET`
  retrieve, `PATCH` merge-update, `PUT` replace, `DELETE` remove,
  `OPTIONS` discover).
- **URL query parameters** that drive server-side semantics:
  `?attrs=`, `?options=keyValues|concise|sysAttrs`, `?q=`, `?geometry=`,
  `?coordinates=`, `?datasetId=`, `?type=`, `?limit=`, `?offset=`,
  `?count=`, `?join=`, `?pick=`, dozens more.
- **HTTP headers** that change interpretation: `NGSILD-Tenant`,
  `NGSILD-Scope`, `Link` (for the `@context`), `Accept`,
  `Authorization`, `Fiware-Service`, `Fiware-Servicepath`.
- **Status codes** with semantic meaning (`201` vs `204`, `400` vs
  `404` vs `409`, partial-success `207`).

DDS has **none** of this. Forwarding over DDS means re-implementing the
HTTP semantic layer inside the message body — and then the receiving
broker has to demarshal it back into the same HTTP-like structure it
already speaks natively. We'd be paying for an extra encoding round-trip
to lose information we already have for free.

### 2.3. Forwarding is request/reply, not pub/sub

DDS's strength is **fire-and-forget streaming**: a publisher writes a
sample and many subscribers may or may not consume it, with the QoS
making the ordering / reliability guarantees explicit. There is no
inherent "this sample replies to that sample" — request/reply was bolted
on later as the DDS-RPC standard and is implemented unevenly across
implementations.

NGSI-LD forwarding is fundamentally:

> "Broker A forwards this request to Broker B and waits for B's response
> with the response body, status code, and headers; if B doesn't reply
> within a timeout, A returns an error."

That is HTTP RPC. Doing it over DDS-RPC means re-creating timeouts,
correlation IDs, error envelopes, partial-failure semantics — all things
HTTP gives us in libcurl in three lines of code.

### 2.4. Federation is small and known; DDS overhead is built for the opposite

A typical NGSI-LD federation has a handful of brokers known to each
other through *registrations* (each broker explicitly tells the others
where to find it). The set is small, mostly static, and we already know
which broker to talk to for which entity range.

DDS's multicast / discovery-server machinery exists to solve the *opposite*
problem: arbitrary participants in an unknown topology finding each
other dynamically. None of that work pays off in a federation; it just
adds startup cost, network chatter, and operational surface (firewalls,
domain IDs, discovery server lifecycle).

### 2.5. QoS is leverage we don't need

Durability, history depth, deadline, lifespan, ownership strength, etc.
are powerful tools when designing a telemetry plane. For point-to-point
request/reply between brokers, **TCP already gives us all we need**:
reliable, ordered byte delivery on a single connection. The whole QoS
surface becomes either irrelevant (we want default reliable + no history)
or a footgun (a misconfigured `durability` and our forwarding starts
replaying old requests on reconnect).

### 2.6. The dependency chain is enormous

To embed DDS in orion-ld we already pull in: Fast-CDR, Fast-DDS,
dev-utils, ddspipe, ddsenabler, FIWARE-DDS-Enabler, plus the IDL toolchain
for type generation. Each is its own release cycle, with its own CI, its
own ABI churn, and its own bug surface (we hit one this week in
`FibonacciServer.py` on Jazzy that took two days to triage).

For a forwarding plugin, this dependency footprint pays for capabilities
we won't use.

---

## 3. What a purpose-built binary IPC plugin gives us instead

The brief is much simpler than DDS allows for. The forwarding layer
needs:

- **A framed binary protocol over TCP** between brokers we already know
  about via registrations.
- **One message type per HTTP verb** (or, more cheaply, a single generic
  "forwarded request" envelope carrying verb, path, headers, query, body
  + a "forwarded response" carrying status, headers, body).
- **Length-prefixed framing** so the receiver knows the boundaries.
- **A correlation id** for matching responses to requests.
- A handful of standard fields (timeout, tenant, scope, link).

That's it. A few hundred lines of code, no external dependency, zero
discovery cost, and the wire format is whatever we make it — including
field-omitted-when-default tricks that we can never play in CDR.

### 3.1. The size comparison

| Aspect | DDS-as-transport | Custom binary IPC |
|---|---|---|
| External deps | 6+ libs, multi-million LOC | None |
| Build-time IDL generation | Required | None |
| Startup cost | Multicast discovery, type registration | TCP connect |
| Wire format | CDR (fixed schema per topic) | Whatever we design (variable, JSON-bytes, msgpack, …) |
| QoS surface | ~20 policies, configurable | Default TCP |
| Failure mode | "Service does not exist" 500ms after publish | Connection refused, instantly |
| Per-hop latency | ~hundreds of µs (CDR + matching + delivery) | ~tens of µs (TCP + framing) |
| Bytes on the wire | CDR overhead + RTPS framing | Whatever we ship + 4-byte length prefix |
| Time-to-ship | Months — model NGSI-LD's surface in IDL, regenerate types, distribute, integrate | Days — single source file, both sides |
| Cross-broker version drift | Recompile required on type change | Add a field; old peers ignore unknown |
| Debuggability | Wireshark via RTPS dissector, partial | `tcpdump`, `curl`, `nc`, hex dump |

### 3.2. Where DDS *is* the right tool — keep using it there

This is **not** an argument against DDS in general or against orion-ld's
existing DDS integration (described in
[`orion-ld-dds.md`](./orion-ld-dds.md)). DDS is the right answer when:

- Orion-LD is exposing entity attributes onto a DDS bus that's already
  populated by ROS 2 / FastDDS / industrial peers — i.e. when the *other
  side* is a DDS application that we have to interoperate with.
- The data is a continuous stream of typed samples (telemetry,
  position, sensor readings) with well-known IDL.
- We want DDS QoS semantics (durability, history, etc.) end-to-end.

That's exactly what `-wip dds` solves today. **Adding DDS as a
transport for *broker-to-broker forwarding* is a different problem and
a poor fit.**

---

## 4. Recommendation

Continue treating DDS as an **integration target** (a way for orion-ld
to talk to existing DDS applications), not as an **internal transport**
(a way for orion-ld instances to talk to each other).

If a binary IPC plugin is desired to reduce HTTP overhead between
federated brokers, design it as a purpose-built protocol: framed binary
over TCP, one envelope per forwarded HTTP request/response, length-prefixed,
correlation-id, transport TLS. That delivers the latency / footprint
benefit the request asks for, in a fraction of the engineering time, with
no external dependency, and without distorting either NGSI-LD's HTTP-
native semantics or DDS's pub/sub strengths.
