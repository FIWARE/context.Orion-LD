# Temporal Representation of Entities
Starting with the Orion-LD release [Beta 2](https://github.com/FIWARE/context.Orion-LD/releases/), Orion-LD implements Temporal Representation of Entities (TRoE), in an experimental state.
The feature is minimally tested and not production-ready, but more or less working and the development team would be more than happy to have it tested, and any bugs reported.

The sink used for TRoE is Postgres, with PostGIS and TimescaleDB extensions.

## Native Temporal Queries

Orion-LD now supports **native temporal entity queries** directly, without requiring Mintaka. You can retrieve the state of an entity at a specific point in time using the standard NGSI-LD temporal API:

```
GET /ngsi-ld/v1/temporal/entities/{entityId}?timerel=before&timeAt=2024-06-15T12:00:00Z
```

### Supported Parameters

| Parameter | Values | Description |
|-----------|--------|-------------|
| `timerel` | `before`, `after`, `between` | **Required.** Temporal relation |
| `timeAt` | ISO 8601 timestamp | **Required.** Reference timestamp |
| `endTimeAt` | ISO 8601 timestamp | Required only for `timerel=between` |
| `options` | `simplified`, `concise` | Output format (default: normalized) |

### Examples

**Point-in-time snapshot** (entity state at or before a given time):
```bash
curl 'localhost:1026/ngsi-ld/v1/temporal/entities/urn:ngsi-ld:Sensor:001?timerel=before&timeAt=2024-06-15T12:00:00Z'
```

**Changes since a timestamp:**
```bash
curl 'localhost:1026/ngsi-ld/v1/temporal/entities/urn:ngsi-ld:Sensor:001?timerel=after&timeAt=2024-06-01T00:00:00Z'
```

**Changes within a time window:**
```bash
curl 'localhost:1026/ngsi-ld/v1/temporal/entities/urn:ngsi-ld:Sensor:001?timerel=between&timeAt=2024-06-01T00:00:00Z&endTimeAt=2024-06-30T23:59:59Z'
```

### Requirements

- TRoE must be enabled (`-troe` flag)
- PostgreSQL/TRoE database must be running and connected
- The entity must have temporal history in the TRoE database

### How It Works

The native temporal query reconstructs an entity's state from the three TRoE tables:

1. **Entity type** — retrieved from the `entities` table at the given timestamp
2. **Attribute values** — uses `DISTINCT ON (id, datasetId) ORDER BY ts DESC` on the `attributes` table to get the latest value for each attribute at or before the requested time
3. **Sub-attributes** — retrieved from `subAttributes` for attributes that have sub-properties

All value types are supported: String, Number, Boolean, Relationship, DateTime, Compound, GeoProperty (all geo types), and LanguageMap.

## Mintaka Compatibility

For more advanced temporal queries (aggregations, pagination of temporal values, etc.), [Mintaka](https://github.com/FIWARE/Mintaka) can still be used as an external temporal query handler on (default) port 8080.

Compatibility with the latest release version of mintaka will always be assured. See the test results at the [mintaka-compatibility github action.](https://github.com/FIWARE/context.Orion-LD/actions/workflows/mintaka-compatibility.yml)

More fine-grained information on compatibility can be found at the [compatibility-matrix](https://github.com/FIWARE/mintaka/blob/main/doc/compatibility/compatibility.md).

## Kafka Consumer for High-Throughput Ingestion

For use cases requiring high-throughput time series ingestion (1,000-10,000 msg/s), Orion-LD includes an optional Kafka consumer subsystem. This allows streaming entity updates via Apache Kafka, bypassing HTTP overhead while preserving full validation, MongoDB entity state updates, TRoE temporal writes, and notification dispatch.

### Architecture

```
Kafka Topic ("orionld-entities")
       |
       v
  Kafka Consumer Thread(s)  [librdkafka, N configurable threads]
       |
       |  rd_kafka_consumer_poll() + Micro-Batching
       v
  Batch Upsert Pipeline
       |
       +-- Validation (3 rounds)
       +-- MongoDB entity state update
       +-- TRoE temporal write (PostgreSQL)
       +-- Notification dispatch
       +-- Kafka offset commit (at-least-once semantics)
```

### Enabling Kafka

Start the broker with the `-kafka` flag:

```bash
orionld -kafka -kafkaBrokerList localhost:9092 -kafkaTopic orionld-entities -troe
```

### Kafka CLI Options

| Option | Default | Description |
|--------|---------|-------------|
| `-kafka` | false | Enable Kafka consumer subsystem |
| `-kafkaBrokerList` | `localhost:9092` | Kafka broker address(es) |
| `-kafkaTopic` | `orionld-entities` | Topic to consume from |
| `-kafkaGroupId` | `orionld-consumer` | Consumer group ID |
| `-kafkaBatchSize` | 100 | Max entities per micro-batch |
| `-kafkaBatchLingerMs` | 50 | Max ms to wait before flushing a batch |
| `-kafkaConsumerThreads` | 2 | Number of consumer threads |

### Message Format

Messages must be valid NGSI-LD normalized JSON — identical to `POST /entityOperations/upsert?options=update`:

**Single entity:**
```json
{
  "id": "urn:ngsi-ld:TemperatureSensor:001",
  "type": "TemperatureSensor",
  "temperature": {
    "type": "Property",
    "value": 23.5,
    "observedAt": "2024-06-15T10:30:00Z"
  }
}
```

**Batch (array of entities):**
```json
[
  { "id": "urn:ngsi-ld:Sensor:001", "type": "Sensor", "temperature": { "type": "Property", "value": 23.5 } },
  { "id": "urn:ngsi-ld:Sensor:002", "type": "Sensor", "temperature": { "type": "Property", "value": 24.1 } }
]
```

**Kafka Message Key:** Use the entity ID to guarantee ordering per entity within a partition.

**Optional Kafka Headers:**
- `NGSILD-Tenant` — for multi-tenancy support
- `Link` — custom @context URL

### Micro-Batching Strategy

Each consumer thread accumulates messages into a local batch. Flush triggers (first one wins):

1. Batch reaches `kafkaBatchSize` entities
2. `kafkaBatchLingerMs` timer expires
3. Empty poll result while batch is non-empty

Backpressure is natural: if the batch upsert pipeline is slow (DB load), polling slows down automatically. Kafka offsets are committed only after successful processing (at-least-once semantics with idempotent upserts).

### Dependencies

The Kafka consumer requires [librdkafka](https://github.com/confluentinc/librdkafka) to be installed. See [External Libraries](external-libraries.md).

## Database setup
To run Orion-LD with TroE enabled, a PostgreSQL with PostGIS and TimescaleDB is needed.
For local installations, this [timescaledb-postgis image](https://hub.docker.com/layers/timescale/timescaledb-postgis/latest-pg12/images/sha256-40be823de6035faa44d3e811f04f3f064868ee779ebb49b287e1c809ec786994?context=explore) is recommended.

To start it, use:
```
docker run -e POSTGRES_USER=orion -e POSTGRES_PASSWORD=orion -e POSTGRES_HOST_AUTH_METHOD=trust timescale/timescaledb-postgis:latest-pg12
```

## Database Migration
A database migration scripts is found in the [database-folder](../../database)
The file [initial.sql](../../database/sql/initial.sql) contains the SQL script for the timescale database. It holds the schema at the state of Orion-LD version 0.7.0.

### Migration
For database migration, a liquibase changelog is provided. This changelog can be used to migrate existing databases to the new schema.
The  changesets use the following ID schema: ```v<MIGRATION_NR>_step_<STEP_NR>```
The ```<MIGRATION_NR>``` is a continuously incrementing number.
All steps of one migration to a certain Orion-LD version should use the same  ```<MIGRATION_NR>```.

### Running the Migration

The database migration can be executed via a liquibase docker container. It needs access to the database.
The following script can be used:
```
docker run 
           -v <DATABASE_FOLDER>:/liquibase/changelog
           liquibase/liquibase 
           --driver=org.postgresql.Driver 
           --url="jdbc:postgresql://<TIMESCALE_HOST>:<TIMESCALE_PORT>/<DATABSE_NAME>"
           --changeLogFile=timescale-changelog_master.xml 
           --username=<TIMESCALE_USERNAME>
           --password=<TIMESCALE_PASSWORD>
           update
```
(Replace ```<DATABASE_FOLDER>``` with a path [database](../../database))

The script needs to be executed for every tenant, as each tenant in held in a database of its own.
The naming schema of the "tenant databases" is:
```<DEFAULT_DATABASE_NAME>_<TENANT_ID>```, e.g., "orion_mytenant".

Every migration has its own changelog file, they are postfixed with the given version. When running on top of a database initialized with the 
[initial.sql](../../database/sql/initial.sql), the [timescale-changelog_master.xml](../../database/timescale-changelog_master.xml) can be used to apply 
all changesets at once. If the db is already updated to a certain version, only the changelogs after that version should be used.

> :warning: Be aware that the runtime of an update can be substantial(minutes to hours) when executed at a huge database. The existence of a an up-to-date 
> backup should be assured!

### Version history

| Version | Description |
| ----------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| v1 | Changed primary keys to include the timestamp in preparation of timescale-hypertable support, added index for (sub-)attributes. |
| v2 | Added the datasetId to the combined primary key and optimizes its datatype. |
| v3 | Add multipoint for attributes and subAttributes table. |
| v4 | Change data types to support the 3rd dimension.  |

## Database Schema

TRoE uses a normalized 3-table schema to store temporal entity data in PostgreSQL.

### Custom Types

```sql
CREATE TYPE ValueType AS ENUM(
    'String',
    'Number',
    'Boolean',
    'Relationship',
    'Compound',
    'DateTime',
    'GeoPoint',
    'GeoMultiPoint',
    'GeoPolygon',
    'GeoMultiPolygon',
    'GeoLineString',
    'GeoMultiLineString',
    'LanguageMap');

CREATE TYPE OperationMode AS ENUM(
    'Create',
    'Append',
    'Update',
    'Replace',
    'Delete');
```

### Table: entities

Stores entity-level records with temporal versioning.

```sql
CREATE TABLE IF NOT EXISTS entities (
    instanceId TEXT NOT NULL,      -- Unique instance identifier (UUID)
    ts TIMESTAMP NOT NULL,         -- Timestamp when record was created
    opMode OperationMode,          -- Operation type (Create, Update, etc.)
    id TEXT NOT NULL,              -- Entity ID (URI)
    type TEXT NOT NULL,            -- Entity type (URI)
    CONSTRAINT entities_pkey PRIMARY KEY (instanceId, ts)
);
```

### Table: attributes

Stores attribute instances with multi-value support via `datasetId`.

```sql
CREATE TABLE IF NOT EXISTS attributes (
    instanceId TEXT NOT NULL,      -- Unique instance identifier for this attribute
    id TEXT NOT NULL,              -- Attribute name (URI)
    opMode OperationMode,          -- Operation type
    entityId TEXT NOT NULL,        -- Parent entity ID (foreign key to entities.id)
    observedAt TIMESTAMP,          -- When the value was observed (optional)
    subProperties BOOL,            -- Has sub-attributes?
    unitCode TEXT,                 -- Unit code (optional)
    datasetId VARCHAR NOT NULL,    -- Dataset identifier for multi-valued attributes
    valueType ValueType,           -- Type of value stored
    text TEXT,                     -- String value
    boolean BOOL,                  -- Boolean value
    number FLOAT8,                 -- Numeric value
    datetime TIMESTAMP,            -- DateTime value
    compound JSONB,                -- Complex/nested value
    geoPoint GEOGRAPHY(POINTZ, 4326),
    geoMultiPoint GEOGRAPHY(MULTIPOINTZ, 4326),
    geoPolygon GEOGRAPHY(POLYGONZ, 4326),
    geoMultiPolygon GEOGRAPHY(MULTIPOLYGONZ, 4326),
    geoLineString GEOGRAPHY(LINESTRINGZ, 4326),
    geoMultiLineString GEOGRAPHY(MULTILINESTRINGZ, 4326),
    ts TIMESTAMP NOT NULL,         -- Record timestamp
    CONSTRAINT attributes_pkey PRIMARY KEY (instanceId, datasetId, ts)
);
```

### Table: subAttributes

Stores sub-attributes (nested properties and relationships within attributes).

```sql
CREATE TABLE IF NOT EXISTS subAttributes (
    instanceId TEXT NOT NULL,      -- Unique instance identifier
    id TEXT NOT NULL,              -- Sub-attribute name (URI)
    entityId TEXT NOT NULL,        -- Parent entity ID
    attrInstanceId TEXT NOT NULL,  -- Parent attribute instance ID
    attrDatasetId VARCHAR NOT NULL,-- Parent attribute dataset ID
    observedAt TIMESTAMP,          -- When the value was observed
    unitCode TEXT,                 -- Unit code (optional)
    valueType ValueType,           -- Type of value stored
    text TEXT,
    boolean BOOL,
    number FLOAT8,
    datetime TIMESTAMP,
    compound JSONB,
    geoPoint GEOGRAPHY(POINTZ, 4326),
    geoMultiPoint GEOGRAPHY(MULTIPOINTZ, 4326),
    geoPolygon GEOGRAPHY(POLYGONZ, 4326),
    geoMultiPolygon GEOGRAPHY(MULTIPOLYGONZ, 4326),
    geoLineString GEOGRAPHY(LINESTRINGZ, 4326),
    geoMultiLineString GEOGRAPHY(MULTILINESTRINGZ, 4326),
    ts TIMESTAMP NOT NULL,
    CONSTRAINT subattributes_pkey PRIMARY KEY (instanceId, ts)
);
```

### Default Index

```sql
CREATE INDEX subattributes_attributeid_index ON subAttributes (attrInstanceId, attrDatasetId);
```

## Performance Tuning

The default schema includes minimal indexes to keep write performance high. Depending on your query patterns, you may want to add additional indexes.

### Recommended Indexes

Add indexes based on your most common query patterns:

| Query Pattern | Recommended Index | SQL |
|--------------|-------------------|-----|
| Temporal history of one entity | attributes by entityId | `CREATE INDEX idx_attr_entityid_ts ON attributes(entityId, ts DESC);` |
| All entities of a type | entities by type | `CREATE INDEX idx_entities_type_ts ON entities(type, ts DESC);` |
| Query by attribute name | attributes by id | `CREATE INDEX idx_attr_id_ts ON attributes(id, ts DESC);` |
| Entity lookup by ID | entities by id | `CREATE INDEX idx_entities_id_ts ON entities(id, ts DESC);` |
| Filter by observedAt | attributes by observedAt | `CREATE INDEX idx_attr_observedat ON attributes(entityId, observedAt DESC);` |
| Geo-queries | spatial index | `CREATE INDEX idx_attr_geopoint ON attributes USING GIST(geoPoint);` |

### Example: Common Index Set

For a typical deployment with mixed read/write workloads:

```sql
-- Essential for temporal entity reconstruction (JOIN performance)
CREATE INDEX idx_attr_entityid_ts ON attributes(entityId, ts DESC);

-- Common queries by entity type
CREATE INDEX idx_entities_type_ts ON entities(type, ts DESC);

-- Entity lookup
CREATE INDEX idx_entities_id_ts ON entities(id, ts DESC);
```

### Trade-offs

| More Indexes | Fewer Indexes |
|--------------|---------------|
| Faster reads | Faster writes |
| More storage | Less storage |
| Slower inserts | Better for high-frequency sensor data |
| Better for analytics | Better for data ingestion |

### Checking Current Indexes

To see existing indexes in your TRoE database:

```sql
SELECT indexname, indexdef
FROM pg_indexes
WHERE tablename IN ('entities', 'attributes', 'subattributes');
```

### Analyzing Query Performance

Use `EXPLAIN ANALYZE` to understand query performance:

```sql
EXPLAIN ANALYZE
SELECT * FROM attributes
WHERE entityId = 'urn:ngsi-ld:Entity:001'
AND ts BETWEEN '2024-01-01' AND '2024-12-31';
```

If you see "Seq Scan" on large tables, consider adding an index for that query pattern.

## Advanced Optimization: Large-Scale Deployments

For deployments with very large datasets (100M+ attribute records), additional optimization strategies beyond indexes may be required.

### Citus for Horizontal Scaling

[Citus](https://www.citusdata.com/) is a PostgreSQL extension that enables horizontal scaling through distributed tables. It has been tested successfully with TRoE datasets exceeding 100 million data points.

```sql
-- Distribute tables by entityId to co-locate entity data
SELECT create_distributed_table('entities', 'id');
SELECT create_distributed_table('attributes', 'entityId', colocate_with => 'entities');
SELECT create_distributed_table('subAttributes', 'entityId', colocate_with => 'entities');
```

**Benefits:**
- Parallel query execution across shards
- Entity data co-located on same worker node (efficient JOINs)
- Linear scalability by adding worker nodes

### Table Partitioning

PostgreSQL native partitioning by time range improves query performance through partition pruning:

```sql
-- Create partitioned attributes table
CREATE TABLE attributes_partitioned (
    LIKE attributes INCLUDING ALL
) PARTITION BY RANGE (ts);

-- Create monthly partitions
CREATE TABLE attributes_2024_01 PARTITION OF attributes_partitioned
    FOR VALUES FROM ('2024-01-01') TO ('2024-02-01');

CREATE TABLE attributes_2024_02 PARTITION OF attributes_partitioned
    FOR VALUES FROM ('2024-02-01') TO ('2024-03-01');
```

**Benefits:**
- Automatic partition pruning (queries only scan relevant partitions)
- Easier data retention (drop old partitions)
- Smaller indexes per partition

### Combined Approach

For maximum performance on large datasets, combine both strategies:

1. **Partition by time** - Monthly or weekly partitions based on `ts`
2. **Distribute with Citus** - Shard by `entityId` across worker nodes
3. **Strategic indexes** - Add indexes based on actual query patterns

This combination has been proven to handle 100M+ attribute records with fast query response times.

### Example SQL

A complete SQL example demonstrating Citus + partitioning + indexes is available at:
[`doc/manuals-ld/examples/citus-example.sql`](examples/citus-example.sql)

This script shows:
- Partitioned attributes table by `observedAt`
- Distribution by `entityId` for Citus
- Default partition for NULL/out-of-range values
- Recommended index set for common query patterns

### When to Consider These Optimizations

| Dataset Size | Recommendation |
|--------------|----------------|
| < 1M records | Default schema + recommended indexes |
| 1M - 10M records | Add indexes, consider partitioning |
| 10M - 100M records | Partitioning recommended |
| > 100M records | Citus + Partitioning + Indexes |
