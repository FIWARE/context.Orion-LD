# High Availability (running more than one broker)

Running several Orion-LD instances behind a load balancer, against one shared MongoDB.

> **Status: not yet available.**
> Cache synchronisation — the piece that makes multi-instance work — is under development.
> Until it is released, **run a single Orion-LD instance**. See
> [Running multiple instances without synchronisation](#running-multiple-instances-without-synchronisation)
> for what goes wrong if you don't, and why it is hard to diagnose.
> The option names below are provisional until the feature is released.

## What an HA setup is

Three parts, and all three are needed:

1. **Two or more Orion-LD instances** (pods, containers, hosts).
2. **A load balancer** in front of them. A request may land on any instance — there is no session
   affinity and none is needed.
3. **MongoDB as a replica set**, shared by all instances.

The third part is not negotiable, and not only because of Orion-LD. If N brokers share a single
standalone `mongod`, the broker tier is redundant but the database is not — losing it takes the
whole deployment down. That is not a highly available system, whatever the broker count says.
Anybody who needs HA needs a replica set for their data anyway.

## Why synchronisation is needed at all

Each instance keeps two caches in RAM:

* the **subscription cache** — every subscription with its match criteria pre-compiled (id
  patterns as regexes, `q` as a parsed tree, `geoQ` as a prepared geometry). Every entity update
  is matched against it to decide what to notify.
* the **registration cache** — every context source registration, likewise pre-compiled. Every
  request consults it to decide what to forward and where.

They exist because matching on every single update, and forwarding on every single request, must
not mean a database round trip.

The catch is that a subscription or registration created on one instance lands in **that
instance's** cache. Without synchronisation the other instances never learn about it. Cache
synchronisation closes that gap: every instance is told about every change, as it happens.

## Requirements

| | |
|---|---|
| MongoDB | A **replica set** (or sharded cluster). Configure as usual with `-rplSet` / `MONGO_REPLICA_SET`. |
| MongoDB version | 3.6 or later. |
| Orion-LD | The same version on every instance. |
| Load balancer | Any. No session affinity required. |

A **single-node replica set** — one `mongod` started with `--replSet` and initialised with
`rs.initiate()` — is enough to satisfy the mechanism, and is convenient for development and
testing. For actual high availability the database obviously needs real redundancy too.

### Already running a standalone MongoDB?

Switching an existing standalone to a replica set is a restart, not a migration — the data is
kept. Add `--replSet` to the `mongod` command line and initialise the set once:

```
mongod --replSet rs0 ...            # add to the existing command line / deployment
mongosh --eval 'rs.initiate()'      # once, against that mongod
```

then point the brokers at it with `-rplSet rs0` / `MONGO_REPLICA_SET=rs0`.

That single node already provides the oplog the synchronisation needs. Growing the set to three
members is what makes the *database* highly available, and can be done later, independently — it
is not a prerequisite for running several brokers.

### Why a replica set specifically

Synchronisation uses MongoDB **change streams**, which are built on the **oplog** — and the oplog
exists only in a replica set. A standalone `mongod` cannot offer change streams at all.

This is a requirement of the synchronisation feature, not of Orion-LD in general. **Running a
single instance against a standalone MongoDB remains fully supported and is unaffected.**

## Enabling it

Cache synchronisation is off by default. Enable it on **every** instance:

```
orionld -rplSet <replicaSetName> -haSync
```

or with the environment variable:

```
ORIONLD_HA_SYNC=TRUE
```

If synchronisation is enabled but the database is not a replica set, the broker refuses to start
and says so. It does not fall back to running unsynchronised — an instance that silently believes
it is part of an HA cluster while missing every remote change is precisely the failure this
feature exists to prevent.

## How it works

Every instance opens a **change stream** against the shared database and is *pushed* every
subscription and registration change as it is written. There is no polling, so there is no
interval to tune and no fixed window during which instances disagree.

* One deployment-level change stream per instance covers **every tenant** (Orion-LD keeps one
  database per tenant) in a single thread.
* Each event carries the operation — insert, update, delete — and the affected document, so the
  receiving instance updates the relevant tenant's cache directly rather than reloading it.
* On startup an instance loads its caches from the database as it always has, then watches from
  that point onward. A newly started instance therefore converges by construction.
* If the connection drops, the stream resumes from where it left off. If it cannot resume, the
  instance reloads the affected cache in full.
* An instance also receives the events for its own changes; applying them again is harmless.

Nothing is sent between broker instances. They never connect to each other, need no knowledge of
each other, and require no additional port, peer list or firewall rule.

## Operational behaviour

| Event | What happens |
|---|---|
| Add an instance | It loads the caches at startup and then watches. No action needed on the others. |
| Remove an instance | Nothing to do. No instance tracks any other. |
| Instance restart | Same as adding one — full load, then watch. |
| MongoDB primary failover | The change stream is resumed automatically against the new primary. |
| Brief network loss | Resumed from where it left off; missed events are delivered. |
| Long network loss | If the stream cannot be resumed, the cache is reloaded in full. |

### Consistency model

Propagation is **eventual, but push-based** — typically within milliseconds of the write being
committed, bounded by replication and delivery, not by any poll interval.

It is *not* instantaneous and must not be treated as such. A client that creates a subscription
and immediately fires an entity update through the load balancer may have the update handled by
an instance that has not yet applied the new subscription. This is inherent to any distributed
cache and is not specific to Orion-LD.

## What cache synchronisation does **not** solve

Cache synchronisation makes every instance *see* the same subscriptions and registrations. Some
behaviour is still per-instance, and matters when planning an HA deployment:

* **Periodic notifications fire per instance.** A subscription that notifies on a timer runs on
  every instance that holds it, so with N instances the receiver gets N notifications per period.
  Change-driven notifications do *not* have this problem — only the instance that handled the
  entity update notifies.
* **Throttling is per-instance state.** With N instances, the effective minimum interval between
  notifications is up to N times more permissive than configured.
* **Subscription counters** (`timesSent`, `timesFailed`, `lastNotification`, ...) accumulate per
  instance and are reconciled to the database per instance, so they must be added together to be
  read globally.

These are known and are being addressed alongside the synchronisation work. They are listed here
because a deployment that scales out will meet them, and it is better to meet them knowingly.

## Running multiple instances without synchronisation

For completeness — this is what happens today, and what will happen if synchronisation is left
disabled in a multi-instance deployment.

The registration cache is loaded once at startup and never refreshed. The subscription cache has
an older refresh mechanism, `-subCacheIval` / `SUBCACHE_IVAL`, which re-reads all subscriptions
periodically; it is **0 (off) by default**.

So each instance is perfectly consistent with itself and out of step with the others, and because
the load balancer spreads requests the symptoms look *intermittent*:

* **Sporadic 404s when dispatching commands.** The common one. An IoT Agent registers itself as a
  context provider for a device's command attributes. On the instance that handled that
  registration the command is forwarded and works; on every other instance there is no matching
  registration and nothing stored locally, so the broker answers `404`.
* **Forwarded queries and updates** miss data held by a context source, for the same reason.
* **Deleted registrations stay alive** on the other instances, which keep forwarding to a context
  source that is no longer registered.
* **Notifications are not sent**, or are sent by only some instances, for subscriptions the others
  have never seen.

There is no configuration that makes this correct — `-subCacheIval` helps subscriptions converge
eventually, and there is no equivalent for registrations at all.

## Summary

* One instance + standalone MongoDB — supported, unaffected by any of this.
* Several instances + load balancer + replica set + `-haSync` — the HA setup, once released.
* Several instances without synchronisation — don't. It fails intermittently, and the failures
  are hard to attribute to their cause.
