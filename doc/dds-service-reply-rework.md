# DDS service reply storage — rework status, blockers and questions for eProsima

This document describes the partial rework of how Orion-LD stores DDS service
request/reply data on its mapped NGSI-LD attribute, what eProsima asked us to
produce, what we delivered, what we could *not* deliver, and the underlying
constraints in the eProsima DDS Enabler that block end-to-end testing of the
new structure today.

It is intended to be the starting point for a conversation with the eProsima
team about a small set of API gaps.

---

## 1. Background

Orion-LD can act as a DDS *service client*: when an NGSI-LD attribute that is
mapped to a DDS service is patched, the broker sends the patched value as a
service request via the eProsima DDS Enabler. When the reply arrives, it is
delivered to the broker through the
`ddsServiceReplyNotification(serviceName, json, requestId, publishTime)`
callback registered with the enabler at startup.

Until this rework, the broker stored the reply by simply attaching a single
`ddsServiceResponse` sub-attribute to the service-mapped attribute, with the
raw enabler payload dropped into its `value`. The patched-in request value sat
on the attribute's bare `value` field. Concretely, after a successful
roundtrip the entity looked like this:

```json
"add_two_ints": {
  "type": "Property",
  "value": { "a": 5, "b": 8 },
  "ddsServiceResponse": {
    "type": "Property",
    "value": {
      "id": "01.0f.d1.e9.39.02.5c.22.00.00.00.00",
      "rr/add_two_intsReply": {
        "data": {
          "0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0": { "sum": 13 }
        },
        "type": "example_interfaces::srv::dds_::AddTwoInts_Response_"
      },
      "type": "fastdds"
    }
  }
}
```

eProsima reported two problems with this:

1. **An infinite loop** when the broker received a *second* consecutive
   service request: every reply re-patched the entity, the patch re-published
   the attribute over DDS, the new request arrived back, and so on.
2. **The stored layout did not match what they wanted to consume.** They
   asked for two parallel sub-attributes — `request` and `reply` — each
   carrying the data plus its full DDS metadata, structured as nested
   NGSI-LD Properties:

```json
"<service_name>": {
  "type": "Property",
  "request": {
    "type": "Property",
    "value": { ... },
    "requestId":        { "type": "Property", "value": <id> },
    "instanceHandleId": { "type": "Property", "value": "<instanceHandleId>" },
    "participantId":    { "type": "Property", "value": "<participantId>" },
    "ddsDataType":      { "type": "Property", "value": "<typeName>" },
    "publishedAt":      { "type": "Property", "value": <epoch seconds> }
  },
  "reply": {
    "type": "Property",
    "value": { ... },
    "requestId":        { "type": "Property", "value": <id> },
    "instanceHandleId": { "type": "Property", "value": "<instanceHandleId>" },
    "participantId":    { "type": "Property", "value": "<participantId>" },
    "ddsDataType":      { "type": "Property", "value": "<typeName>" },
    "publishedAt":      { "type": "Property", "value": <epoch seconds> }
  }
}
```

---

## 2. What this branch ships

### 2.1 Infinite loop fix — done

`src/lib/orionld/dds/ddsPublishAttribute.cpp` and
`src/lib/orionld/dds/ddsPublishAttributes.cpp` now early-return if
`orionldState.ddsSample == true`. That flag was already set by every DDS
callback entry point (`ddsServiceReplyNotification`, `ddsNotification`,
`ddsTopicNotification`, `ddsServiceNotification`, `ddsActionSubAttributeUpdate`)
but nothing read it. The guard breaks the cycle for every DDS-callback-driven
re-patch path, not just the service one.

### 2.2 Stored reply structure — done

`src/lib/orionld/dds/ddsServiceReplyNotification.cpp` now constructs and
merge-patches the entity with the new `request`/`reply` sub-attribute layout
shown above. Key pieces:

- `extractReplyMetadata()` parses the eProsima reply envelope and pulls out:
  - `participantId` ← top-level `id`
  - `ddsDataType`   ← `rr/<service>Reply.type`
  - `instanceHandleId` ← the single key under `rr/<service>Reply.data` (FastDDS `InstanceHandle_t`)
  - `value`            ← the value under that `instanceHandleId` key
- `buildSubAttribute()` wraps a payload + metadata block into a nested NGSI-LD
  Property.
- `stringPropertyNode()` / `integerPropertyNode()` are small helpers for the
  per-field nested Properties.
- The merge-patch tree passed to `orionldPatchEntity2()` writes both `request`
  and `reply` in a single atomic update.
- Falls back to storing the raw enabler tree as the reply value if the
  envelope shape doesn't match the documented structure (defensive — never
  drop data on the floor).

### 2.3 Carrying the request payload through to the reply path — done

`DdsServiceInstance` (in `src/lib/orionld/types/DdsService.h`) was extended:

```c
typedef struct DdsServiceInstance
{
  uint64_t                    requestId;
  KAlloc                      kalloc;        // dedicated allocator
  Kjson                       kjson;         // kjson context built on 'kalloc'
  KjNode*                     requestTree;   // cloned request payload
  int64_t                     publishedAt;   // wall-clock seconds since epoch
  struct DdsServiceInstance*  next;
} DdsServiceInstance;
```

Each in-flight request owns its own KAlloc/Kjson buffer pair so the cloned
request `KjNode` tree survives from the request thread until the reply
arrives on the (separate) DDS callback thread, well past the lifetime of
`orionldState.kalloc`. `ddsService.cpp` builds it; `ddsServiceReplyNotification`
reads it and releases the buffer with `kaBufferReset(&kalloc, KFALSE)` + `free()`.

This is what lets the broker reconstruct the *request* sub-attribute when the
reply arrives — without it, the original payload would have been long since
freed.

---

## 3. What we could *not* deliver

### 3.1 `instanceHandleId` and `participantId` on the request side

eProsima asked for the `request` sub-attribute to carry the same six metadata
fields as `reply` (`value`, `requestId`, `instanceHandleId`, `participantId`,
`ddsDataType`, `publishedAt`). We can populate four of them:

| field          | source                                                                                  |
|----------------|------------------------------------------------------------------------------------------|
| `value`        | clone of the original PATCH payload (kept alive in the per-instance KAlloc/Kjson buffer) |
| `requestId`    | filled by the enabler in `send_service_request`'s out-parameter                          |
| `publishedAt`  | `time(NULL)` captured at request submission                                              |
| `ddsDataType`  | `serviceP->requestType` from the broker's service config                                 |

The other two are **not exposed by the eProsima enabler** at request submission
time:

- `instanceHandleId`: the reply envelope JSON shows it (the 16-dot key under
  `rr/<service>Reply.data` — a FastDDS `InstanceHandle_t`), so the value evidently
  exists somewhere inside the enabler at the moment a request is being prepared.
  There is no way to read it from the public `DDSEnabler` API.
- `participantId`: conceptually the broker's own DDS participant GUID prefix.
  Knowable in principle, but not exposed via any public `DDSEnabler` accessor.

So today the `request` sub-attribute is built with **4 of the 6** fields
eProsima asked for. The two missing fields are intentionally omitted rather
than zero-filled, to avoid storing fake values.

**Question for eProsima:** is there a way to expose `instanceHandleId` and
`participantId` on the client side at request submission time? The cleanest fix
would be a companion to `send_service_request` that fills a small struct
(`instanceHandleId`, `participantId`) by reference, mirroring how `request_id`
is returned today — or a public `DDSEnabler::participant_guid_prefix()`
accessor for the participant id.

### 3.2 The attribute's top-level `value` is left alone

eProsima's example layout has no top-level `value` field on the service
attribute — only the `request` and `reply` sub-attributes. The reply path
issues a JSON Merge Patch and intentionally **does not** touch the existing
top-level `value`, so whatever the user originally PATCHed (e.g.
`{"a":5,"b":8}`) is still present alongside the new sub-attributes after
the reply lands.

This is cosmetic — the `request` sub-attribute already carries the same
payload — but it's a deviation from the requested layout. The merge-patch
could explicitly clear the field (e.g. with a JSON Merge Patch `null`), but
that would be a more invasive change that likely wants its own discussion
with eProsima first.

### 3.3 End-to-end functional test of the new structure

`test/functionalTest/cases/0000_ld/dds/dds_service_reply_full_roundtrip.test`
exercises the full roundtrip:

1. ftClient is started, types and service announced.
2. The broker is started — its `ddsInit` calls `announce_service()` early so
   schemas land in the enabler's `schemas_` map before any discovery race.
3. PATCH the service-mapped attribute → broker sends a real DDS service
   request → ftClient receives it.
4. ftClient sends a DDS reply via `send_service_reply()`.
5. Broker's `ddsServiceReplyNotification` fires, builds the new
   `request`/`reply` sub-attribute structure, and merge-patches the entity.
6. Test GETs the entity and asserts the line-by-line REGEXPECT shape.
7. Step 4-6 repeat with a second PATCH/reply, exercising the loop guard.

**This test depends on a patched FIWARE-DDS-Enabler.** See section 4 below
for the patch and how to install it. Without that patch, the broker's
`send_service_request()` is rejected at step 3 by `EnablerParticipant::publish_rpc()`
and no reply ever arrives — `dds_service_reply.test` keeps passing because
it tolerates that failure mode, but `dds_service_reply_full_roundtrip.test`
fails on the entity assertion.

In addition, the test relies on a few QoS / config tweaks that ship as part
of this branch:

- `--ddsService` in `scripts/configFile.sh` accepts an optional 5th and 6th
  field (`requestType,replyType`) so the broker can pre-load schemas.
- `--ddsTypesDirectory` in `scripts/configFile.sh` writes a
  `dds.ngsild.typesDirectory` entry into the broker config.
- `ddsServiceQuery` in `ddsInit.cpp` returns a default
  `serialized_qos = "reliability: true\ndurability: false\n..."` matching
  what ftClient sends, so the broker's writers and ftClient's readers
  actually match instead of failing with `INCOMPATIBLE QOS`.
- ftClient's types and service announcement happen in `--SHELL-INIT--`,
  before the broker is started, so the broker's early-announce doesn't
  race ftClient's type loading.

---

## 4. The eProsima patch (proposed upstream)

The full structural rework end-to-end test is unblocked by a one-place patch
in `EnablerParticipant::publish_rpc()` — the FIWARE-DDS-Enabler change that
allows a process which has called `announce_service()` to also publish
service requests for that service, even before DDS discovery has marked an
external counterparty.

The patch currently sits in the working tree of the
`~/git/DDS/FIWARE-DDS-Enabler` repo, on a branch named
`orionld/broker-as-client-publish-rpc-relax` cut from `main`. It is
intentionally **not committed** so the eProsima team can review it as a
plain `git diff`. Concrete diff:

```diff
--- a/ddsenabler_participants/src/cpp/EnablerParticipant.cpp
+++ b/ddsenabler_participants/src/cpp/EnablerParticipant.cpp
@@ -209,17 +209,37 @@ bool EnablerParticipant::publish_rpc(
     auto it = services_.find(rpc_info->service_name);
     if (it == services_.end())
     {
-        // There is no case where none of the service topics are discovered and yet the publish should be done
+        // The service has neither been discovered externally nor announced
+        // locally. Without an entry in services_ there is no topic to publish on.
         EPROSIMA_LOG_ERROR(DDSENABLER_ENABLER_PARTICIPANT,
                 "Failed to publish data in service " << rpc_info->service_name << " : service does not exist.");
         return false;
     }
 
-    if (!it->second->external_server && rpc_info->service_type == ServiceType::REQUEST)
+    //
+    // To publish a service request, the enabler needs to know about an external
+    // counterparty for the service. That counterparty is normally recorded by
+    // DDS discovery as 'external_server'. For an application that wants to act
+    // as a service CLIENT only - and has called announce_service() purely to
+    // pre-load the request/reply type schemas via the user's serviceQuery /
+    // typeQuery callbacks - allow publishing as soon as the local enabler is
+    // aware of the service ('enabler_as_server' is set by the early-return
+    // branch in announce_service()).
+    //
+    // Without this relaxation a process that has loaded its schemas via
+    // announce_service() but is not itself a server can never call
+    // send_service_request() successfully, because get_serialized_data() (the
+    // only place that consults schemas_) requires the schemas to have been
+    // loaded *and* publish_rpc requires external_server, and the only way to
+    // load schemas without going through announce_service() is to wait for
+    // discovery, which races the publish.
+    //
+    if (!it->second->external_server && !it->second->enabler_as_server &&
+        rpc_info->service_type == ServiceType::REQUEST)
     {
         EPROSIMA_LOG_ERROR(DDSENABLER_ENABLER_PARTICIPANT,
                 "Failed to publish data in service " << rpc_info->service_name <<
-                " : service is only announced on the enabler side.");
+                " : service is neither announced locally nor discovered externally.");
         return false;
     }
 
```

To build and install (from the patched working tree):

```bash
cd ~/git/DDS/FIWARE-DDS-Enabler/build/ddsenabler_participants
cmake --build .            # builds as $USER, no sudo needed
sudo cmake --install .     # installs to /usr/local/lib (root needed)
```

To revert to a stock eProsima library (e.g. before running the broker
without the patched .so), discard the working-tree edit and re-install:

```bash
cd ~/git/DDS/FIWARE-DDS-Enabler
git checkout -- ddsenabler_participants/src/cpp/EnablerParticipant.cpp
cd build/ddsenabler_participants
cmake --build . && sudo cmake --install .
```

### Known limitation that this patch DOES NOT fix

After applying this patch, the broker can call `send_service_request()`
successfully and the full reply roundtrip works as documented in §3.3. The
remaining limitations from §3.1 still hold: `instanceHandleId` and
`participantId` on the request side need additional eProsima API surface
that this patch does not introduce. They are absent on the request
sub-attribute today.

### Known limitation in `orionldPatchEntity2` (not eProsima)

On the second roundtrip, the entity's `reply.requestId` and
`request.requestId` Property values stay at their *first* values even
though the broker's merge-patch tree carries the new ones. The
`reply.value` field updates correctly, only the leaf integer fields
inside nested sub-sub-attribute Properties don't. This appears to be a
NGSI-LD merge-patch behaviour for nested integer Property `value` fields
in `orionldPatchEntity2` and is unrelated to the DDS rework — captured
as a TODO and the test currently asserts the actual stable behaviour.

---

## 5. The eProsima blocker: broker-as-DDS-client schema loading vs. discovery

> **Status:** resolved by the patch in §4. This section is retained as
> background context for the discussion with eProsima — it explains *why*
> the patch is necessary and the alternative paths considered.

To send a service request, eProsima's `Handler::get_serialized_data` requires
the type schema for the request topic to be present in its internal
`schemas_` map. That map is populated as a side effect of the
`announce_service` flow:

```
announce_service
  → query_service_nts_ (calls user's ServiceQuery callback for type names)
    → fill_service_type_nts_
      → fill_topic_struct_nts_
        → handler_->get_type_identifier
          → user's TypeQuery callback (loads .bin from disk)
          → register_type_nts_
          → schemas_.insert(...)
```

The broker is the *client*, never the server, but to load the schemas it
must call `announce_service` itself. There are two states a service can be
in inside `EnablerParticipant::services_`:

| state                                  | `enabler_as_server` | `external_server` | what works                                            |
|----------------------------------------|---------------------|-------------------|--------------------------------------------------------|
| announced first by broker              | `true`              | `false`           | schemas loaded — but `publish_rpc` (line 218) refuses: "service is only announced on the enabler side" |
| discovered first via DDS               | `false`             | `true`            | `publish_rpc` allowed — but the schemas were never loaded; `get_serialized_data` fails: "schema not available" |
| announced first, then discovered       | `true`              | `true`            | should work — but in practice a discovery notification never fired in our test once we had announced first |

The issue is that the broker-as-client is split across two mutually exclusive
preconditions inside the eProsima participant:

- `publish_rpc()` requires `external_server == true`. Only the discovery path
  sets it.
- `get_serialized_data()` requires the schema in `schemas_`. Only the
  `announce_service` path loads it.

There is no public API today that lets a *client* both load schemas and be
recognized as having an external counterparty without racing the discovery
mechanism.

We also tried calling `announce_service` from inside `ddsServiceNotification`
(after the broker received the discovery notification, when the types are
known). That path **deadlocks**: `ddsServiceNotification` runs on a DDS
Enabler thread that already holds the participant mutex; `announce_service`
re-acquires the same mutex.

**Questions for eProsima:**

1. Can `EnablerParticipant::publish_rpc` be relaxed so that an
   `enabler_as_server == true && external_server == false` service is also
   allowed to publish requests? (Or: a separate `external_client` /
   `is_client` flag?)
2. Alternatively, is there a public API to register a type schema with the
   handler/registry **without** going through `announce_service`? Something
   like `DDSEnabler::register_type(const std::string& type_name, const
   uint8_t* serialized_type, uint32_t size)` would let the broker load
   schemas eagerly and then let normal discovery mark `external_server`.
3. Is `announce_service` safe to call from inside the
   `service_notification` callback, or must it always be invoked from a
   non-DDS-Enabler thread? If the latter, the documentation should call
   that out and ideally there should be a "first time external server
   discovered" callback that runs on a worker thread, not the participant
   thread.

---

## 6. Broker-side scaffolding that ships with this branch

The following pieces all exist on this branch and are needed for the
end-to-end test to work end-to-end (in addition to the eProsima patch
in §4):

- **`src/lib/orionld/dds/ddsTypeLoad.{h,cpp}`** — small module that loads
  `.bin` type files on demand from a configured directory, using
  eProsima's safe-filename convention (`:` / `/` / `\` → `_`).
- **`scripts/configFile.sh --ddsTypesDirectory <path>`** — adds a
  `dds.ngsild.typesDirectory` entry to the broker config; `ddsInit`
  reads it and calls `ddsTypesDirectorySet()`.
- **`scripts/configFile.sh --ddsService` extended to 6 fields**
  `name,entityType,entityId,attribute,requestType,replyType` — the
  extra two fields populate `requestType`/`replyType` on the
  `DdsService` struct so the broker knows what to query/load at startup.
- **`ddsServicesPopulateFromConfig()` in `ddsPrePopulateDb`** — walks
  `dds.ngsild.services` synchronously at the top of `ddsInit`, before
  the enabler is created, so the `DdsService` linked list exists by
  the time the early-announce loop runs.
- **`ddsTypeQuery` callback in `ddsInit.cpp`** — wired to `ddsTypeLoad`,
  returns the bytes of the requested type to the enabler.
- **`ddsServiceQuery` callback in `ddsInit.cpp`** — fills the enabler's
  `ServiceInfo` from the broker's `DdsService` lookup. Also returns a
  default `serialized_qos = "reliability: true\n…"` so the broker's
  service writers come up Reliable, matching what ftClient (and the ROS2
  default) expects. Without this default, the broker is BE while peers
  are Reliable and the endpoints fail to match with `INCOMPATIBLE QOS`.
- **Early-announce loop at the end of `ddsInit`** — for each
  `DdsService` whose `requestType` and `replyType` are known from the
  config, the broker calls
  `ddsEnabler->announce_service(name, ROS2)` immediately after
  `create_dds_enabler` returns. This forces the type-query and
  service-query callbacks to fire so the schemas are loaded into the
  enabler's `schemas_` map *before* any DDS discovery happens. With the
  eProsima patch in §4, the broker is now allowed to publish requests
  for these "self-announced" services.
- **Refactored ftClient `postDdsType.cpp`** to delegate to the shared
  `ddsTypeLoad` module so the type loading code lives in one place
  (linked into both binaries via `orionld_dds`).

These are all legitimate additions, independent of the test scaffolding —
the broker actually needs every one of them to act as a DDS service
client. The eProsima patch alone isn't enough; the broker has to know
which schemas to load, when to load them, and what QoS to register
endpoints with. This branch provides all of that.

---

## 7. Unrelated cleanup landed at the same time

- `src/lib/apiTypesV2/HttpInfo.cpp`: `HttpInfo::fill` now guards
  `getStringFieldF(boP, CSUB_MIMETYPE)` with `hasField()` (matching the
  pattern on the surrounding lines), eliminating the noisy
  `E: Runtime Error (string field 'mimeType' is missing in BSONObj ...)`
  for subscriptions stored without an explicit `mimeType`.
- Three test files had `expires`/`expiresAt` dates that had silently rotted
  past today (`1661_bad_input_on_notification_error/*.test` and
  `1705_csub_cache_objects/csub_cache_string_filter_update.test`).
  Bumped to `2031-04-05`.

These are not related to the DDS rework; they were tripped over while
running the suite.

---

## 8. Synchronous DDS semantics: `?ddsSync=true|false`

The follow-up branch `dds/ddsSync` adds a per-request opt-in/out that makes
the broker treat `PATCH /entities/{entityId}` as a synchronous DDS
round-trip for attributes mapped to a DDS service. The intent (from
eProsima) is that a DDS-aware client sees the HTTP 2xx only after the
DDS reply has been merged, rather than the prior behavior of "update
mongo, fire notifications, TRoE, then *maybe* update again when a reply
shows up".

### 8.1 Scope

- Only `PATCH /entities/{entityId}` (the merge-entity route in
  `orionldPatchEntity2.cpp`).
- `PATCH /entities/{entityId}/attrs/{attr}` stays fire-and-forget - this is
  the intentional escape hatch for clients that do NOT want to block.
  BATCH / PUT / POST routes are unchanged.

### 8.2 Defaults and overrides

| Broker state       | URL param            | Effective behavior |
|--------------------|----------------------|--------------------|
| `-wip dds` on      | (none)               | sync               |
| `-wip dds` on      | `?ddsSync=true`      | sync (explicit)    |
| `-wip dds` on      | `?ddsSync=false`     | async (opt-out)    |
| `-wip dds` off     | (any)                | no DDS attrs exist |

The default follows the broker's DDS mode. A DDS-integrated broker assumes
its clients are DDS-aware and want coherent DDS state. The URL param lets
any single request flip the other way.

### 8.3 Failure semantics

- **No DDS server discovered** -> `503 Service Unavailable` with
  `detail = <serviceName>`. No mongo write, no notifications, no TRoE.
- **Reply timeout** -> `504 Gateway Timeout`. Same atomicity. Timeout is
  configurable via `dds.ngsild.syncTimeoutMs` (default `5000`).
- **Late reply after timeout** -> dropped with a `W:` trace.

**Atomicity applies to the whole PATCH payload.** If the caller sends one
DDS-tied attr and three regular attrs in the same PATCH with
`?ddsSync=true`, a DDS failure rolls back all four. That's intentional:
otherwise the client ends up with a partial commit and no clean way to
reason about what's actually in the entity.

### 8.4 Implementation notes

- `DdsServiceInstance` gained `syncMode`, `pthread_mutex_t`, `pthread_cond_t`,
  `replyReceived`, `replyTree`, and envelope metadata fields.
- The reply callback (`ddsServiceReplyNotification`) branches on `syncMode`:
  in sync mode it parses the reply into the instance's own kalloc, signals
  the waiter, and returns without doing the merge-patch (the PATCH thread
  does it).
- Per-`DdsService` `instancesMtx` serializes push (request) and pop
  (reply or sync-timeout). Removes a latent race from the pre-existing
  async flow too.
- `orionldPatchEntity2.cpp` calls `ddsSyncPatchEntityProcess` right after
  `pCheckEntity`/`previousValues`, before `distOpRequests`/mongo. On
  success, sets `orionldState.ddsSample = true` so the post-mongo
  `ddsPublishAttributes` skips republishing (the sync path already did
  the DDS round-trip).
- `ddsReplyBuild.{h,cpp}` holds the sub-attribute Property builders shared
  between the async and sync paths (previously static in the reply
  notification file).

### 8.5 Known asymmetry

`PATCH /entities/{id}?ddsSync=true` and
`PATCH /entities/{id}/attrs/{attr}` give different semantics for the same
logical update. That's deliberate - we wanted the async escape hatch
without an extra CLI knob. Document this prominently for users; it's the
kind of thing that catches people with mixed clients.
