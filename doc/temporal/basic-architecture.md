# Temporal Module Architecture

## Overview

Orion-LD implements Temporal Representation of Entities (TRoE) using a dual-database architecture:

- **MongoDB** — stores the current state of entities (latest attribute values)
- **PostgreSQL** (with PostGIS) — stores the full temporal history of all entity changes

Every entity creation, update, or deletion is recorded in both databases. MongoDB reflects the "now" state, while PostgreSQL keeps the complete timeline.

## Data Flow

### Write Path (Entity Updates)

```
HTTP Request / Kafka Message
       |
       v
  Validation (payload check, @context expansion)
       |
       v
  MongoDB Upsert (current entity state)
       |
       v
  TRoE Write (PostgreSQL temporal history)
       |    entities table   — entity ID, type, timestamp, operation mode
       |    attributes table — attribute values with all types and geo data
       |    subAttributes    — nested properties/relationships
       |
       v
  Notification Dispatch (subscriptions)
```

### Read Path (Temporal Queries)

```
GET /ngsi-ld/v1/temporal/entities/{entityId}?timerel=before&timeAt=<ts>
       |
       v
  Parameter Validation (timerel, timeAt, endTimeAt)
       |
       v
  PostgreSQL Queries (3 queries):
       |
       +-- 1. Entity type at timestamp (entities table)
       +-- 2. Latest attribute values (DISTINCT ON + ORDER BY ts DESC)
       +-- 3. Sub-attributes for matching attributes
       |
       v
  Response Building (PGresult → KjNode NGSI-LD entity tree)
       |
       v
  Format Transformation (normalized / simplified / concise)
```

## Database Schema

TRoE uses three tables — see [TRoE documentation](../manuals-ld/troe.md) for the full schema.

| Table | Purpose | Primary Key |
|-------|---------|-------------|
| `entities` | Entity-level records (ID, type, operation) | `(instanceId, ts)` |
| `attributes` | Attribute values with all types | `(instanceId, datasetId, ts)` |
| `subAttributes` | Nested properties/relationships | `(instanceId, ts)` |

## Ingestion Paths

### REST API (Standard)

Standard NGSI-LD API operations (POST, PATCH, PUT, DELETE on entities) trigger TRoE writes automatically when the `-troe` flag is enabled.

### Kafka Consumer (High-Throughput)

For high-volume time series ingestion (1,000-10,000+ msg/s), the Kafka consumer subsystem provides a more efficient path:

```
Kafka Topic
       |
  Consumer Thread(s) [librdkafka]
       |
  Micro-Batching (configurable size/linger)
       |
  Batch Upsert Pipeline (same as POST /entityOperations/upsert)
```

The Kafka path reuses the exact same validation and database pipeline as the REST API, ensuring data consistency. See [TRoE documentation](../manuals-ld/troe.md) for Kafka configuration details.

## Point-in-Time Entity Reconstruction

The key SQL pattern for reconstructing an entity at a specific point in time:

```sql
-- Get the latest value for each attribute at or before timestamp
SELECT DISTINCT ON (id, "datasetId")
  id, "valueType"::text, text, boolean, number, datetime, compound,
  "observedAt", "unitCode", "datasetId", "subProperties",
  ST_AsGeoJSON("geoPoint") as "geoPoint",
  ...
FROM attributes
WHERE "entityId" = $1 AND ts <= $2 AND "opMode" != 'Delete'
ORDER BY id, "datasetId", ts DESC;
```

`DISTINCT ON` combined with `ORDER BY ts DESC` selects the most recent row for each unique `(id, datasetId)` combination, effectively giving the "snapshot" of all attribute values at the requested time.

## Supported Value Types

All NGSI-LD value types are supported in temporal storage and retrieval:

| ValueType | Storage Column | NGSI-LD Type |
|-----------|---------------|--------------|
| String | `text` | Property |
| Number | `number` | Property |
| Boolean | `boolean` | Property |
| DateTime | `datetime` | Property |
| Compound | `compound` (JSONB) | Property |
| Relationship | `text` | Relationship |
| LanguageMap | `compound` (JSONB) | LanguageProperty |
| GeoPoint | `geoPoint` (PostGIS) | GeoProperty |
| GeoPolygon | `geoPolygon` (PostGIS) | GeoProperty |
| GeoMultiPoint | `geoMultiPoint` (PostGIS) | GeoProperty |
| GeoMultiPolygon | `geoMultiPolygon` (PostGIS) | GeoProperty |
| GeoLineString | `geoLineString` (PostGIS) | GeoProperty |
| GeoMultiLineString | `geoMultiLineString` (PostGIS) | GeoProperty |
