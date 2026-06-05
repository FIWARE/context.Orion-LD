# DDS in Orion-LD — User Guide

This guide is aimed at someone who already knows DDS / Fast-DDS / ROS 2 (topics,
services, actions, IDL types, QoS), and wants to know how those map onto the
NGSI-LD entity model that Orion-LD exposes over REST.

The DDS integration is built on top of [FIWARE-DDS-Enabler] (an in-process
DDS participant on the broker side), and is gated by the `-wip dds` CLI
flag — the feature is "work in progress" but functional end-to-end for the
three modes documented here.

[FIWARE-DDS-Enabler]: https://github.com/eProsima/FIWARE-DDS-Enabler

---

## 1. Overview

Orion-LD can act as a peer of any DDS application, in three modes:

| DDS concept | NGSI-LD mapping | Broker role | Direction |
|---|---|---|---|
| **Topic** (pub/sub) | A DDS topic ↔ one *attribute* of one *entity*. Publishes/receives the `value` field of that attribute as the DDS sample. | Publisher and subscriber. | Bidirectional. A REST update of the attribute publishes to DDS. A DDS sample on the topic updates the attribute. |
| **Service** (request/reply) | A DDS service ↔ one *attribute*. PATCHing the attribute issues a DDS service request whose payload is the attribute `value`. The reply is merged back into the same attribute. | **Client only.** | Outbound trigger via REST → reply lands back as a sub-Property. |
| **Action** (long-running goal) | A DDS action ↔ one *attribute*. PATCHing the attribute issues a goal; each goal is a separate instance keyed by `datasetId="urn:goal:<uuid>"`, with feedback / result / status as envelope sub-Properties. An optional `endpoint` sub-attribute auto-creates a temporary subscription streaming the goal's lifecycle to the initiator. On completion the instance is removed — and the whole attribute when it was the last in-flight goal (re-created by the next goal PATCH); history stays in TRoE. | **Client only.** | Outbound trigger via REST → async feedback / result / status; optional temp-subscription notifications. |

In all three modes the **wire payload** on the DDS side is just the JSON
serialization of `attribute.value`. Sub-attributes (metadata, `observedAt`,
`unitCode`, …) and the attribute envelope itself (`type`, `datasetId`, …) are
**not** transmitted. See [§5](#5-the-value-as-body-convention).

### 1.1. Why the broker is client-only for services and actions

For DDS topics the broker is a fully symmetric peer — it can publish and it
can subscribe, because in either case it's just shuttling JSON values into
and out of an attribute.

For DDS **services** and **actions** the broker is exclusively the *client*.
There is no mode in which orion-ld acts as the DDS server that serves a
request or executes a goal. The reason is architectural: an NGSI-LD broker
only knows about **entities, subscriptions and registrations**. It has no
extension point for hand-written business logic that would compute a reply
or run a goal — that's the job of an application, not a generic context
broker.

To expose a DDS service or action *from* the FIWARE side, an external
application (typically an **IoT Agent**) provides the server. How much DDS
the agent itself has to know depends on the service:

- For a pure-compute service like `add_two_ints`, the agent could in
  principle stay DDS-unaware. The broker would need to be extended to act
  as a DDS-server proxy, forwarding incoming DDS requests to the agent via
  an NGSI-LD *registration* and shipping the agent's NGSI-LD reply back out
  on the DDS wire. Nothing in the NGSI-LD model rules this out, but
  orion-ld does not implement it today.
- For an action like "move the robot to position X", the agent will almost
  certainly need to speak DDS itself — to drive the underlying ROS 2 / DDS
  stack and stream feedback in real time. In that case the agent *is* the
  DDS server end-to-end, and orion-ld stays out of the DDS server side
  entirely (it may still appear in the NGSI-LD picture as the place that
  holds the entity's state, but the DDS wire goes agent ↔ robot, with the
  broker not involved on that path).

Either way, today: orion-ld is **client-only** for DDS services and
actions. The server side lives elsewhere.

---

## 2. NGSI-LD in 60 seconds (for DDS people)

An NGSI-LD **Entity** is a JSON-LD object with an `id`, a `type`, and a flat
collection of **Attributes** (named fields). Each Attribute is either a
**Property** (carrying a `value`) or a **Relationship** (carrying an `object`
URI pointing to another entity). Attributes can themselves carry
sub-Properties (metadata) — `observedAt`, `unitCode`, `lang`, etc.

```jsonc
{
  "id": "urn:ngsi-ld:robot:r1",
  "type": "Robot",
  "fib": {                            // an Attribute
    "type": "Property",
    "value": { "order": 10 },         // the attribute's value
    "observedAt": "2026-05-13T17:30:00Z"   // a sub-Property (metadata)
  }
}
```

Attributes are CRUD'd over REST. The endpoints relevant to DDS are listed in
[§11](#11-which-rest-endpoints-trigger-dds).

For DDS purposes you only need to remember: **one attribute = one DDS
endpoint** (topic, service, or action), and the JSON `value` is what travels.

---

## 3. Starting the broker with DDS enabled

```
orionld -wip dds [-mongocOnly] [other-flags]
```

When `-wip dds` is set, the broker reads `~/.orionld` at startup, spins up an
embedded DDS participant via the FIWARE-DDS-Enabler, and registers the
topics / services / actions declared in the config. Without `-wip dds` the
file is ignored and no DDS interaction occurs.

Runtime requirements:

- A running FastDDS implementation reachable on the configured domain
  (multicast or discovery server).
- The FIWARE-DDS-Enabler shared libraries on the broker's `LD_LIBRARY_PATH`.
- For services/actions: matching IDL `.bin` type files in the directory
  pointed to by `typesDirectory` (see [§12](#12-type-discovery)).

---

## 4. The configuration file

Orion-LD reads `~/.orionld` (a JSON file) at startup. The shape:

```jsonc
{
  "dds": {
    "ddsmodule": {           // passed verbatim to the Fast-DDS / ddsenabler runtime
      "dds": {
        "domain": 0,
        "allowlist": [ { "name": "*" } ],
        "blocklist": [ { "name": "add_blocked_topics_list_here" } ]
      },
      "topics": {
        "name": "*",
        "qos": {
          "durability":  "TRANSIENT_LOCAL",
          "reliability": "RELIABLE",
          "history-depth": 20
        }
      },
      "ddsenabler": null,
      "specs": {
        "threads": 12,
        "logging": { "stdout": false, "verbosity": "info" }
      }
    },
    "ngsild": {               // NGSI-LD ↔ DDS mappings (orion-ld owns this)
      "topics":   { "<TopicName>":   { "entityType": "...", "entityId": "...", "attribute": "..." } },
      "services": { "<ServiceName>": { "entityType": "...", "entityId": "...", "attribute": "..." } },
      "actions":  { "<ActionName>":  { "entityType": "...", "entityId": "...", "attribute": "..." } },
      "typesDirectory": "/abs/path/to/types",
      "syncTimeoutMs":  5000
    }
  },
  "troe": { ... }            // unrelated to DDS, may be empty
}
```

The relevant fields for DDS users:

| Field | Meaning |
|---|---|
| `dds.ddsmodule.dds.domain` | DDS domain ID. Must match every other participant you want to interoperate with. |
| `dds.ddsmodule.topics.qos` | Default QoS applied to all topics the broker creates. |
| `dds.ddsmodule.specs.threads` | DDS thread pool size. |
| `ngsild.topics["<TopicName>"]` | Maps the DDS topic `<TopicName>` to an `(entityType, entityId, attribute)` triple. |
| `ngsild.services["<ServiceName>"]` | Same, for DDS services. |
| `ngsild.actions["<ActionName>"]` | Same, for DDS actions. |
| `ngsild.typesDirectory` | Directory of pre-built `.bin` IDL type files (required for services and actions — see [§12](#12-type-discovery)). |
| `ngsild.syncTimeoutMs` | Default timeout (ms) for synchronous service requests. |

### 4.1. The `configFile.sh` helper

Writing this JSON by hand is tedious. `scripts/configFile.sh` generates it
from a flat CLI:

```bash
$REPO_HOME/scripts/configFile.sh                                        \
    --ddsTopic   "Camera,Camera,urn:ngsi-ld:camera:cam1,shutterSpeed"   \
    --ddsService "Adder,Robot,urn:ngsi-ld:robot:r1,sum"                 \
    --ddsAction  "Fibonacci,Robot,urn:ngsi-ld:robot:r1,fib"             \
    --ddsTypesDirectory "$HOME/.orionld-types"                          \
    --ddsSyncTimeoutMs 5000                                             \
    > ~/.orionld
```

Each `--ddsTopic` / `--ddsService` / `--ddsAction` takes a comma-separated
tuple `<endpoint name>,<entity type>,<entity id>,<attribute name>`. You can
pass the flag multiple times.

---

## 5. The value-as-body convention

When the broker publishes an attribute on a DDS topic, **only the JSON value
of the attribute is put on the wire** — the attribute envelope and any
sub-attributes are stripped. Conversely, when a DDS sample arrives, its
payload is interpreted as a new value for the mapped attribute.

```jsonc
// REST: PATCH attribute "shutterSpeed" on entity cam1
{
  "type": "Property",
  "value": { "fNumber": 5.6, "speedMs": 0.002 },
  "observedAt": "2026-05-13T17:30:00Z",
  "unitCode": "C26"
}

// On the DDS topic "Camera":
{ "fNumber": 5.6, "speedMs": 0.002 }
// ^ only the value object travels. observedAt, unitCode, type are stripped.
```

Consequences you should know:

1. **Sub-attributes are invisible over DDS.** If a REST client updates
   `attribute.observedAt` or any other sub-Property *without* changing
   `attribute.value`, nothing is published to DDS. Sub-attributes are an
   NGSI-LD-side concern only.
2. **`attribute.value` must be a JSON object** to be publishable. Scalar
   values (string, number, boolean) are not published — the broker logs a
   warning and skips them. This is a current limitation: the IDL types the
   FastDDS side discovers are structs, and structs need keyed fields.
3. **Type compatibility is on you.** The shape of `attribute.value` must
   match the IDL type the topic is bound to. The broker does not validate
   field-by-field; mismatched shapes will fail at the FastDDS marshalling
   layer.
4. **Loop protection is built in.** When a DDS sample arrives and gets
   merged into the entity as an attribute update, the broker does **not**
   re-publish that update back to DDS. Without this guard you'd have an
   infinite loop. Practical effect: a DDS-originated update never
   re-appears on the same topic.

---

## 6. Tutorial — DDS Topic (pub/sub)

We map a DDS topic `Camera` to entity `urn:ngsi-ld:camera:cam1`, attribute
`shutterSpeed`.

### 6.1. Configure

```bash
$REPO_HOME/scripts/configFile.sh \
    --ddsTopic "Camera,Camera,urn:ngsi-ld:camera:cam1,shutterSpeed" \
    > ~/.orionld
orionld -wip dds
```

### 6.2. Outbound: REST → DDS

Create the entity once, then update the attribute:

```bash
# Initial create (also publishes if value is an object)
curl -X POST http://localhost:9999/ngsi-ld/v1/entities \
    -H "Content-Type: application/json" \
    -d '{
          "id":   "urn:ngsi-ld:camera:cam1",
          "type": "Camera",
          "shutterSpeed": {
            "type":  "Property",
            "value": { "fNumber": 5.6, "speedMs": 0.002 }
          }
        }'

# Update — also publishes
curl -X PATCH http://localhost:9999/ngsi-ld/v1/entities/urn:ngsi-ld:camera:cam1/attrs/shutterSpeed \
    -H "Content-Type: application/json" \
    -d '{ "value": { "fNumber": 8.0, "speedMs": 0.001 } }'
```

What goes on the DDS wire: `{"fNumber":8.0,"speedMs":0.001}`.

### 6.3. Inbound: DDS → entity

When a DDS publisher emits a sample on topic `Camera`, the broker:

1. Looks up the topic in `~/.orionld` → finds `(Camera, cam1, shutterSpeed)`.
2. Builds an attribute update with `value` = the sample.
3. Auto-creates the entity if it doesn't exist (`type` defaults to the
   configured `entityType`).
4. Merge-patches the attribute on the entity.

You can immediately `GET` the entity to see the latest sample as the
attribute's value.

---

## 7. Tutorial — DDS Service (request/reply)

We map a DDS service `Adder` to attribute `sum` on entity `urn:ngsi-ld:robot:r1`.

### 7.1. Configure

```bash
$REPO_HOME/scripts/configFile.sh \
    --ddsService "Adder,Robot,urn:ngsi-ld:robot:r1,sum" \
    --ddsTypesDirectory "$HOME/.orionld-types" \
    --ddsSyncTimeoutMs 5000 \
    > ~/.orionld
orionld -wip dds
```

### 7.2. Triggering the request

PATCH the mapped attribute with the request as its `value`:

```bash
curl -X PATCH http://localhost:9999/ngsi-ld/v1/entities/urn:ngsi-ld:robot:r1/attrs/sum \
    -H "Content-Type: application/json" \
    -d '{ "value": { "a": 3, "b": 4 } }'
```

Behaviour:

- **Synchronous (default)**: the REST call blocks until the DDS reply
  arrives, then returns. The reply is merged into the `sum` attribute as a
  sub-Property `ddsServiceReply` (carrying `value` = the reply payload,
  plus `replyTime`).
- **Asynchronous**: append `?ddsSync=false`. The REST call returns
  immediately; the reply lands later as a sub-Property update via the same
  notification mechanism.

Timeout is governed by `ngsild.syncTimeoutMs` from the config (default 5 s).
On timeout, REST returns 504.

### 7.3. Inspecting the result

```bash
curl http://localhost:9999/ngsi-ld/v1/entities/urn:ngsi-ld:robot:r1
```

```jsonc
{
  "id": "urn:ngsi-ld:robot:r1",
  "type": "Robot",
  "sum": {
    "type": "Property",
    "value": { "a": 3, "b": 4 },
    "ddsServiceReply": {
      "type":  "Property",
      "value": { "result": 7 },
      "replyTime":     { "type": "Property", "value": 1715617200.123 },
      "instanceHandleId": { "type": "Property", "value": "..." }
    }
  }
}
```

---

## 8. Tutorial — DDS Action (long-running goal with feedback)

DDS actions are the trickiest mapping, because a single attribute may host
**many concurrent in-flight goals**. The broker handles this by giving each
goal its own attribute *instance*, distinguished by `datasetId`.

We map a DDS action `Fibonacci` to attribute `fib` on entity
`urn:ngsi-ld:robot:r1`.

### 8.1. Configure

```bash
$REPO_HOME/scripts/configFile.sh \
    --ddsAction "Fibonacci,Robot,urn:ngsi-ld:robot:r1,fib" \
    --ddsTypesDirectory "$HOME/.orionld-types" \
    > ~/.orionld
orionld -wip dds
```

### 8.2. Sending a goal

PATCH the attribute. The `value` is the goal payload (e.g. for Fibonacci,
the order).

```bash
curl -X PATCH http://localhost:9999/ngsi-ld/v1/entities/urn:ngsi-ld:robot:r1/attrs/fib \
    -H "Content-Type: application/json" \
    -d '{ "value": { "order": 5 } }'
```

The broker generates a `goalId` (UUID), sends the goal over DDS, and
returns immediately. From that moment a new attribute instance exists on
the entity, with `datasetId = "urn:goal:<goalId>"`.

#### Streaming this goal's lifecycle — the `endpoint` sub-attribute

Add an `endpoint` sub-attribute to the goal PATCH and the broker
auto-creates a **temporary** subscription (cache-only, not persisted) scoped
to the goal's entity: the feedback / result / status updates are delivered to
that endpoint for the life of the goal, and the subscription is torn down
automatically when the goal terminates. No separate `POST /subscriptions`
needed.

```bash
curl -X PATCH http://localhost:9999/ngsi-ld/v1/entities/urn:ngsi-ld:robot:r1 \
    -H "Content-Type: application/json" \
    -d '{ "fib": { "type": "Property",
                   "value": { "order": 5 },
                   "endpoint": { "type": "Property", "value": "http://my-app:7000/notify" } } }'
```

`endpoint` is control metadata: it is not sent on the DDS wire (only `value`
is) and it is removed together with the attribute when the goal completes.

The temporary subscription is **datasetId-scoped to this goal** in two
independent ways:

- its `watchedAttributes` entry is `"<attr>@<goalDatasetId>"`, so it is
  *triggered* only by changes to **this** goal's instance; and
- its top-level `datasetId` *projects* each notification body down to this
  goal's own feedback / result / status envelope.

So when several goals run on the same action attribute at the same time, each
initiator receives **only its own goal's** updates — concurrent goals do not
cross-talk.

> **NOTE.** datasetId in `watchedAttributes` (syntax `"<attr>@<datasetId>"`,
> with `"<attr>@@none"` for the default instance) is an Orion-LD extension —
> not yet part of the ETSI NGSI-LD API; an addition has been proposed, so the
> syntax may change.

### 8.3. Feedback / result / status arrive as envelope sub-Properties

As the DDS action server emits feedback samples and finally a result with
a status, the broker stores each in the goal's attribute instance:

```jsonc
{
  "fib": [
    {
      "type":  "Property",
      "value": { "order": 5 }            // the *default* instance (the goal request)
    },
    {
      "type":      "Property",
      "datasetId": "urn:goal:9b…",       // this is the goal's instance
      "ddsActionFeedback": {             // updated each time a feedback sample arrives
        "type":  "Property",
        "value": { "partial_sequence": [0, 1, 1, 2, 3] },
        "publishedAt": { "type": "Property", "value": 1715617200.456 }
      },
      "ddsActionResult": {               // appears once the server returns the result
        "type":  "Property",
        "value": { "sequence": [0, 1, 1, 2, 3, 5] },
        "publishedAt": { "type": "Property", "value": 1715617200.789 }
      },
      "ddsActionStatus": {               // status transitions (succeeded, aborted, executing, …)
        "type":  "Property",
        "value": { "code": "succeeded", "message": "" },
        "publishedAt": { "type": "Property", "value": 1715617200.790 }
      }
    }
  ]
}
```

### 8.4. Retrieving a specific goal's state

Use the standard NGSI-LD `?datasetId` query parameter:

```bash
curl 'http://localhost:9999/ngsi-ld/v1/entities/urn:ngsi-ld:robot:r1?datasetId=urn:goal:9b…'
```

When `-wip dds` is on, the broker also accepts `?goal=<uuid>` as a synonym
that auto-prefixes `urn:goal:`:

```bash
curl 'http://localhost:9999/ngsi-ld/v1/entities/urn:ngsi-ld:robot:r1?goal=9b…'
```

### 8.5. Cancelling a goal

DELETE the goal's attribute instance. The broker sees `datasetId =
urn:goal:<uuid>` on a DDS-action attribute and emits a cancel-request to
the action server before removing the local instance.

```bash
curl -X DELETE \
    'http://localhost:9999/ngsi-ld/v1/entities/urn:ngsi-ld:robot:r1/attrs/fib?datasetId=urn:goal:9b…'

# Or, equivalently:
curl -X DELETE \
    'http://localhost:9999/ngsi-ld/v1/entities/urn:ngsi-ld:robot:r1/attrs/fib?goal=9b…'
```

### 8.6. Completion and cleanup

When a goal reaches a terminal status (succeeded / canceled / aborted /
rejected / timeout / failed), the broker:

1. delivers the final notification (if a temporary `endpoint` subscription exists),
2. tears down that temporary subscription,
3. removes the goal's `datasetId` instance — and when it was the **last** instance,
   removes the **whole attribute** (`fib` disappears from the entity).

A subsequent goal PATCH re-creates the attribute, so the entity only carries
`fib` while one or more goals are in flight. The goal's full history (every
feedback / result / status update) remains in the **temporal (TRoE) database** —
only the live representation is removed.

> The instance/attribute removal is a direct DB write, so it does not itself
> emit a TRoE record of the *disappearance* (the lifecycle leading up to it is
> recorded). See §13.

---

## 9. DDS as part of an atomic PATCH

For `PATCH /entities/{id}` specifically (the **entity-level** PATCH, not the
single-attribute one), the DDS call is **synchronous and load-bearing**. When
`-wip dds` is active and the caller has not opted out with `?ddsSync=false`,
the broker:

1. Executes the DDS service / action calls embedded in the payload **first**.
2. If any of them fails (timeout, 503/504), the **entire PATCH is rejected**
   *before* anything is written to mongo. That includes non-DDS attributes
   in the same payload — they are not persisted.
3. Only if all DDS calls succeed does the PATCH go on to merge the changes
   into the entity and fire subscription notifications.

This is intentional: PATCH `/entities/{id}` is the API surface for
DDS-driven workflows where the entity state and the upstream DDS world must
move together or not at all.

The single-attribute PATCH (`PATCH /entities/{id}/attrs/{attr}`) keeps
fire-and-forget semantics — it's the explicit escape hatch when the caller
wants async behaviour. To force fire-and-forget on the entity-level PATCH
too, pass `?ddsSync=false`.

---

## 10. Subscribing to what's going on in the DDS setup

Inbound DDS samples are merged into entities via the broker's normal
attribute-update path (`orionldPutAttribute` under the hood). The DDS
flow joins the standard alteration / notification pipeline at exactly the
same point as a REST update, which means:

- **NGSI-LD subscriptions fire on DDS-driven updates** with no extra
  configuration. A subscription that watches `(entityId, attribute)` will
  notify on:
  - A DDS topic sample arriving on the mapped attribute.
  - A DDS service reply landing on the attribute as a `ddsServiceReply`
    sub-Property.
  - A DDS action's feedback / result / status arriving on the goal's
    attribute instance.

- The notification payload is the standard NGSI-LD notification —
  no DDS-specific framing. The goal id is the instance's `datasetId`
  (`urn:goal:<uuid>`); the publish time and other DDS envelope metadata are
  available as sub-Properties of the attribute (see
  [§8.3](#83-feedback--result--status-arrive-as-envelope-sub-properties)).

### 10.1. Example: subscribing to Fibonacci goal status changes

```bash
curl -X POST http://localhost:9999/ngsi-ld/v1/subscriptions \
    -H "Content-Type: application/ld+json" \
    -d '{
          "id":    "urn:ngsi-ld:Subscription:fib-progress",
          "type":  "Subscription",
          "entities":            [ { "type": "Robot" } ],
          "watchedAttributes":   [ "fib" ],
          "notification": {
            "attributes": [ "fib" ],
            "format":     "normalized",
            "endpoint":   { "uri": "http://my-app:7000/notify",
                            "accept": "application/json" }
          },
          "@context": "https://uri.etsi.org/ngsi-ld/v1/ngsi-ld-core-context-v1.8.jsonld"
        }'
```

Every feedback sample from the DDS action server, every status transition,
and the final result will arrive at `http://my-app:7000/notify` as a normal
NGSI-LD notification carrying the full `fib` attribute (with all goal
instances). To narrow down to a single goal, filter by `datasetId` on the
consumer side, or use `q`-filters on the sub-Properties — e.g.
`q=fib.ddsActionStatus.value.code=="succeeded"`.

The subscription itself is plain NGSI-LD: it has no notion of DDS, doesn't
care that the updates originated from DDS, and works the same whether the
attribute is driven by REST writes or by a DDS publisher.

---

## 11. Which REST endpoints trigger DDS

Not every NGSI-LD operation talks to DDS today. The current set of
DDS-aware service routines:

| Verb + path | Effect on DDS |
|---|---|
| `POST /entities` | For every attribute in the body that's mapped to a topic / service / action, performs the corresponding publish / request / goal-send. |
| `POST /entityOperations/create` | Same, per entity in the batch. |
| `POST /entityOperations/upsert` | Same. |
| `POST /entityOperations/update` | Same. |
| `PUT  /entities/{id}` | Same — every mapped attribute in the body is published. |
| `PUT  /entities/{id}/attrs/{attr}` | If the attribute is mapped, publishes / requests / sends-goal. |
| `PATCH /entities/{id}` | Same — every mapped attribute in the body. |
| `PATCH /entities/{id}/attrs/{attr}` | Same — single attribute. This is the canonical way to drive services / actions. |
| `DELETE /entities/{id}/attrs/{attr}?datasetId=urn:goal:<uuid>` | Only for DDS Actions: cancels the upstream goal before deleting the instance. |

Endpoints that **do not** touch DDS today: GETs (entity / attribute /
subscriptions), DELETE entity, DELETE attribute *without* a goal datasetId,
subscription / registration CRUD, temporal API.

---

## 12. Type discovery (`.bin` files)

For DDS topics, FastDDS picks up the type via discovery the first time a
peer publishes — orion-ld doesn't need anything more than the topic name.

For DDS **services and actions**, the broker has to be able to *send*
typed requests/goals before any reply has flowed back. There's no discovery
peer to learn the type from, so the IDL has to be available locally:

- The `typesDirectory` from `~/.orionld` points to a directory of
  pre-serialised IDL types (`.bin` files), one per type name.
- Files used by the orion-ld functests live in
  `test/functionalTest/ddsTypes/`. The naming convention is
  `<typeName>.bin` where `<typeName>` is the DDS IDL type used on the
  request / goal topic.
- The `.bin` files are produced by `fastddsgen` from the matching `.idl`.
  Use any standard FastDDS toolchain (or copy them from a ROS 2 / Vulcanexus
  install).

If you forget the directory or it's missing a type, service / action calls
will fail with a "type not found" error in the broker log and the REST
caller will see a 503 or 504.

---

## 13. Caveats & known issues

- **The broker cannot introduce new DDS types or topics on its own.**
  There is no runtime / REST API in orion-ld to upload an IDL, register a
  new type, or create a brand-new topic from scratch. DDS itself requires
  every participant in the network to have the type definition statically,
  which means a new (type, topic) pair always needs source-code changes
  and a recompile *somewhere* in the network (the publisher, the
  subscriber, or both). The broker is no different — it joins existing DDS
  topics whose types are already defined in the network. In practice this
  means: design the IDL and roll it out across the DDS side first, then
  add the `(topic, entity, attribute)` mapping in `~/.orionld` on the
  orion-ld side.
- **Value must be a JSON object.** Publishing scalars on a DDS topic is
  silently skipped with a warning in the log.
- **No sub-attributes on the wire.** See [§5](#5-the-value-as-body-convention).
  If your application needs `observedAt`, `unitCode`, etc. on the DDS
  consumer side, embed them inside `value`.
- **One attribute per endpoint.** Each topic / service / action maps to
  exactly one `(entityId, attribute)` pair. To expose the same DDS topic on
  multiple entities, configure it multiple times under different topic
  names that share the same underlying DDS topic — there's no built-in
  fan-out.
- **Action notifications are not yet goal-scoped.** A subscription on an
  action attribute (including the temp `endpoint` one) fires on every change,
  but the notification *body* carries the attribute's default instance, not
  the changed goal's per-instance feedback/result/status. Datasetid-scoped
  notification *projection* is planned; until then, read goal detail via
  `GET …?datasetId=urn:goal:<uuid>` or the temporal API.
- **Goal completion isn't recorded in TRoE.** The per-goal lifecycle
  (feedback/result/status updates) is recorded, but the final removal of the
  instance/attribute is a direct DB write and emits no TRoE "deletion" entry.
- **`FibonacciServer.py` crashes on Jazzy.** The Python action server
  shipped with FIWARE-DDS-Enabler has a known `rclpy` issue on Jazzy that
  hits before any goal is sent — see
  [`eprosima_fibonacci_server_jazzy_bug.md`](./eprosima_fibonacci_server_jazzy_bug.md).
  Workaround until fixed upstream: use a C++ action server (note that the
  enabler uses a non-standard topic-naming convention; see the bug report
  for details).
- **The `-wip` flag is intentional.** The DDS feature set is still
  evolving — wire formats, type-discovery semantics, and the mapping
  schema may change before the `-wip` qualifier is dropped.

---

## 14. Reference: `configFile.sh` CLI

```
Usage: configFile.sh [options]
  --ddsTopic   <topic>,<entity type>,<entity id>,<attribute name>     (repeatable)
  --ddsService <name>,<entity type>,<entity id>,<attribute name>      (repeatable)
  --ddsAction  <action>,<entity type>,<entity id>,<attribute name>    (repeatable)
  --ddsTypesDirectory <absolute path to directory of .bin type files>
  --ddsSyncTimeoutMs  <milliseconds>
  --troe <id,idPattern,type1+type2+...,attr1+attr2+...>               (repeatable, unrelated to DDS)
  -u   Usage
```

Pass each `--dds*` flag once per endpoint you want to register. The
resulting JSON is written to stdout; pipe to `~/.orionld` and start the
broker with `-wip dds`.

---

## 15. End-to-end docker-compose example

A self-contained example demonstrating a broker + a Fast-DDS publisher
sharing a topic lives in
`test/functionalTest/cases/0000_ld/dds/dds_publish_post_entity.test`. The
disabled action-roundtrip test
(`test/functionalTest/cases/0000_ld/dds/dds_action_full_roundtrip.test.DISABLED`)
shows the full broker ↔ ROS 2 action-server flow with feedback / result /
status materialisation. Both are useful as recipes.
