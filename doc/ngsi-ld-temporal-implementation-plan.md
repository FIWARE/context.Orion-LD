# NGSI-LD Temporal API - Implementation Plan

> Based on: ETSI GS CIM 009 V1.9.1, OpenAPI v1.8.1, Orion-LD source code (as of March 2026)

## Table of Contents

- [1. Status Overview](#1-status-overview)
- [2. Detailed Gap Analysis](#2-detailed-gap-analysis)
- [3. Implementation Plan](#3-implementation-plan)

---

## 1. Status Overview

| # | Method | Path | Status | Source File |
|---|--------|------|--------|-------------|
| 1 | POST | `/temporal/entities` | **Implemented** (minor gaps) | `orionldPostTemporalEntities.cpp` |
| 2 | GET | `/temporal/entities` | **Implemented** | `orionldGetTemporalEntities.cpp` |
| 3 | GET | `/temporal/entities/{entityId}` | **Implemented** | `orionldGetTemporalEntity.cpp` |
| 4 | DELETE | `/temporal/entities/{entityId}` | **Not implemented** (501 stub) | `orionldDeleteTemporalEntity.cpp` |
| 5 | POST | `/temporal/entities/{entityId}/attrs` | **Not implemented** (501 stub) | `orionldPostTemporalAttributes.cpp` |
| 6 | DELETE | `.../attrs/{attrId}` | **Not implemented** (501 stub) | `orionldDeleteTemporalAttribute.cpp` |
| 7 | PATCH | `.../attrs/{attrId}/{instanceId}` | **Not implemented** (501 stub) | `orionldPatchTemporalAttributeInstance.cpp` |
| 8 | DELETE | `.../attrs/{attrId}/{instanceId}` | **Not implemented** (501 stub) | `orionldDeleteTemporalAttributeInstance.cpp` |
| 9 | POST | `/temporal/entityOperations/query` | **Implemented** | `orionldPostTemporalQuery.cpp` |

**Legend:**
- **Implemented** = Core functionality and most parameters working
- **Implemented (minor gaps)** = Functional, one FIXME in code
- **501 stub** = Returns 501 "Not Implemented"

---

## 2. Detailed Gap Analysis

### 2.1 POST /temporal/entities (orionldPostTemporalEntities.cpp)

**Status:** Implemented with minor gaps

**What works:**
- Entity ID and type are validated (`pCheckEntityId`, `pCheckEntityType`)
- Attributes validated and expanded via `pCheckAttribute` (Array and Object)
- TRoE database populated via `troePostEntities()`
- Upsert logic: 201 for new entity, 204 for existing (`mongocEntityLookup`)
- `Location` header on 201 Created
- `local` URI parameter registered (line 523 in ServiceInit)

**What's missing:**

| Gap | Priority | Details |
|-----|----------|---------|
| location/observationSpace/operationSpace | Medium | FIXME in code (line 115): special attributes not validated/processed |
| Error code 422 | Low | Per spec, 422 Unprocessable Entity should be returned when operation unavailable |

---

### 2.2 GET /temporal/entities (orionldGetTemporalEntities.cpp)

**Status:** Implemented

**What works:**
- Multi-entity temporal query via TRoE PostgreSQL
- Two-phase architecture: entity discovery with pagination, then per-entity pgTemporalEntityQuery/Build
- Entity filtering: `type`, `id` (list), `idPattern` (regex)
- Temporal filtering: `timerel` (before/after/between), `timeAt`, `endTimeAt`
- `timeproperty` (observedAt/modifiedAt/createdAt)
- `attrs` filtering (only requested attributes)
- `lastN` - last N instances per attribute/dataset
- Pagination: `limit`, `offset`
- `count` - NGSILD-Results-Count header
- `pick`/`omit` - inclusion/exclusion projection (mutual exclusivity validated)
- `datasetId` - dataset filtering (temporal-specific, arrays preserved)
- `lang`, `format` (normalized/concise/simplified), `options` (sysAttrs, temporalValues)
- `local` URI parameter registered
- `q` parameter - NGSI-LD query language translated to SQL via `troeQStringToSql`
- `geometry`/`georel`/`coordinates`/`geoproperty` - geo-queries via PostGIS
- `aggrMethods`/`aggrPeriodDuration` - aggregation (avg, min, max, sum, sumsq, stddev, distinctCount)
- Validation: timerel+timeAt mandatory, type or attrs mandatory, endTimeAt for between

**What's still missing:**

| Parameter | Priority | Details |
|-----------|----------|---------|
| `orderBy` | Medium | Sort by entity member |
| `scopeQ` | Low | Scope query |
| `csf` | Low | Context source filter |
| `expandValues`/`jsonKeys` | Low | Special expansion |
| `collation` | Low | ICU collation |
| `splitEntities`/`entityMap`/`entityMapLifetime` | Low | Distributed entities and EntityMap |

---

### 2.3 GET /temporal/entities/{entityId} (orionldGetTemporalEntity.cpp)

**Status:** Implemented

**What works:**
- Entity ID extracted from URL and validated (`pCheckUri`)
- `timerel` (before/after/between), `timeAt`, `endTimeAt` validated
- `timeproperty` - `timeColumnForTimeproperty()` maps to SQL columns (`observedat` for observedAt/default, `ts` for modifiedAt/createdAt)
- `attrs` filtering via `attrsFilter()` builds `AND id IN (...)` SQL clause
- `lastN` - window function `ROW_NUMBER() OVER (PARTITION BY id, datasetid ORDER BY <timeCol>)` with `WHERE rn <= lastN`
- `count` - `NGSILD-Results-Count` header via `orionldHeaderAdd()`
- PostgreSQL query via `pgTemporalEntityQuery()` with 3 result sets (entity, attrs, subAttrs)
- Entity building via `pgTemporalEntityBuild()` with:
  - All value types: String, Number, Boolean, DateTime, Compound, Relationship, LanguageMap, Geo*
  - `instanceId` per attribute instance
  - `observedAt` with PG timestamp -> ISO 8601 conversion
  - `unitCode` and `datasetId`
  - Sub-attributes with matching via `attrInstanceId`
- Output formats: simplified, concise, normalized
- `temporalValues` transformation: array-of-instances -> `{ "type": "...", "values": [[val, ts], ...] }`
- `sysAttrs` option and `lang` parameter
- 404 when entity not found, 501 when TRoE not enabled
- Sorting by dynamic `timeCol` instead of hardcoded `ts`
- `pick` parameter: post-processing via `pickForEntity()` - filters attributes by name
- `omit` parameter: post-processing via `omitForEntity()` - removes named attributes
- `pick`/`omit` mutual exclusivity: 400 error when both provided
- `datasetId` filtering: `datasetTemporalEntityFix()` removes non-matching instances (arrays preserved)
- `timerel`/`timeAt` optional: without these parameters, all attribute instances are returned
- `aggrMethods`/`aggrPeriodDuration` - aggregation post-processing

**What's still missing:**

| Parameter | Priority | Details |
|-----------|----------|---------|
| `local` | Low | URI param registered but no logic behind it |

---

### 2.4 DELETE /temporal/entities/{entityId} (orionldDeleteTemporalEntity.cpp)

**Status:** 501 stub

**To implement:**
- Extract and validate entity ID from URL
- Check if entity exists in TRoE (404 if not)
- Delete all temporal data from PostgreSQL (TRoE): entities, attributes, sub-attributes tables
- Optional: also delete from MongoDB (depends on architecture decision)
- 204 No Content on success

**Affected files:**
- `src/lib/orionld/serviceRoutines/orionldDeleteTemporalEntity.cpp` - main logic
- `src/lib/orionld/troe/` - new function `pgTemporalEntityDelete()` or similar
- `src/lib/orionld/service/orionldServiceInit.cpp` - remove `notImplemented = true`

---

### 2.5 POST /temporal/entities/{entityId}/attrs (orionldPostTemporalAttributes.cpp)

**Status:** 501 stub

**To implement:**
- Extract and validate entity ID from URL
- Check if entity exists (404 if not)
- Parse and validate request body as `EntityTemporalFragment`
- Validate and expand attributes via `pCheckAttribute`
- Insert new attribute instances into TRoE
- 204 No Content on success

**Affected files:**
- `src/lib/orionld/serviceRoutines/orionldPostTemporalAttributes.cpp` - main logic
- `src/lib/orionld/troe/` - new/extended TRoE functions
- `src/lib/orionld/service/orionldServiceInit.cpp` - remove `notImplemented = true`

---

### 2.6 DELETE /temporal/entities/{entityId}/attrs/{attrId} (orionldDeleteTemporalAttribute.cpp)

**Status:** 501 stub

**To implement:**
- Extract and validate `entityId` and `attrId` from URL
- Parse URI parameters `datasetId` and `deleteAll`
- Check if entity and attribute exist (404 if not)
- Deletion logic:
  - `deleteAll=true`: delete all instances of the attribute
  - `datasetId` provided: delete only the specific dataset
  - Neither: delete the default attribute instance
- Delete attribute data from TRoE PostgreSQL (attributes + sub-attributes)
- 204 No Content on success

**Affected files:**
- `src/lib/orionld/serviceRoutines/orionldDeleteTemporalAttribute.cpp` - main logic
- `src/lib/orionld/troe/` - new function for attribute deletion
- `src/lib/orionld/service/orionldServiceInit.cpp` - remove `notImplemented = true`
- `src/lib/orionld/types/OrionLdRestService.h` - possibly new URI parameter flags for `deleteAll`, `datasetId`

---

### 2.7 PATCH /temporal/entities/{entityId}/attrs/{attrId}/{instanceId} (orionldPatchTemporalAttributeInstance.cpp)

**Status:** 501 stub

**To implement:**
- Extract and validate `entityId`, `attrId`, `instanceId` from URL
- Parse request body as `EntityTemporalFragment`
- Check if entity, attribute, and instance exist (404 if not)
- Partial update of the attribute instance in TRoE
- 204 No Content on success

**Affected files:**
- `src/lib/orionld/serviceRoutines/orionldPatchTemporalAttributeInstance.cpp` - main logic
- `src/lib/orionld/troe/` - new function for instance update
- `src/lib/orionld/service/orionldServiceInit.cpp` - remove `notImplemented = true`

---

### 2.8 DELETE /temporal/entities/{entityId}/attrs/{attrId}/{instanceId} (orionldDeleteTemporalAttributeInstance.cpp)

**Status:** 501 stub

**To implement:**
- Extract and validate `entityId`, `attrId`, `instanceId` from URL
- Check if entity, attribute, and instance exist (404 if not)
- Delete single attribute instance from TRoE
- 204 No Content on success

**Affected files:**
- `src/lib/orionld/serviceRoutines/orionldDeleteTemporalAttributeInstance.cpp` - main logic
- `src/lib/orionld/troe/` - new function for instance deletion
- `src/lib/orionld/service/orionldServiceInit.cpp` - remove `notImplemented = true`

---

### 2.9 POST /temporal/entityOperations/query (orionldPostTemporalQuery.cpp)

**Status:** Implemented

**What works:**
- Request body parsed as `Query` with `temporalQ` object
- Entity selectors (type, id, idPattern) extracted from `entities` array
- Attributes extracted from `attrs` array and expanded
- `temporalQ` with timerel, timeAt, endTimeAt, timeproperty parsed and validated
- Two-phase query: entity discovery via `pgTemporalEntitiesQuery`, then per-entity `pgTemporalEntityQuery` + `pgTemporalEntityBuild`
- Post-processing: compact, datasetId filter, format transform, temporalValues, sysAttrs, pick/omit
- Pagination via URL parameters (limit, offset, count)
- `q` parameter (NGSI-LD query language)
- `geoQ` (geo-queries via PostGIS)
- `aggrMethods`/`aggrPeriodDuration` aggregation
- 200 OK with EntityTemporal[] array

**What's still missing:**

| Parameter | Priority | Details |
|-----------|----------|---------|
| `scopeQ` | Low | Scope query |

---

## 3. Implementation Plan

### Phase 1: Complete Existing Endpoints (Priority: High)

Complete the already-functional endpoints with remaining parameter gaps.

#### 1.1 POST /temporal/entities - Fix FIXME

| Step | Description | Effort |
|------|-------------|--------|
| 1.1.1 | Recognize and validate `location`, `observationSpace`, `operationSpace` attributes | Medium |

**Critical files:**
- `src/lib/orionld/serviceRoutines/orionldPostTemporalEntities.cpp`

---

### Phase 2: Simple CRUD Endpoints (Priority: High)

Endpoints that require simple DB operations rather than complex queries.

#### 2.1 DELETE /temporal/entities/{entityId}

| Step | Description | Effort |
|------|-------------|--------|
| 2.1.1 | Implement TRoE delete function `pgTemporalEntityDelete()` | Medium |
| 2.1.2 | Implement service routine | Low |
| 2.1.3 | Remove `notImplemented` flag in ServiceInit | Trivial |

#### 2.2 POST /temporal/entities/{entityId}/attrs

| Step | Description | Effort |
|------|-------------|--------|
| 2.2.1 | Attribute validation and expansion (analogous to POST entities) | Medium |
| 2.2.2 | TRoE insert for new attribute instances | Medium |
| 2.2.3 | Implement service routine | Medium |

#### 2.3 DELETE /temporal/entities/{entityId}/attrs/{attrId}

| Step | Description | Effort |
|------|-------------|--------|
| 2.3.1 | Register URL parameters `deleteAll` and `datasetId` | Low |
| 2.3.2 | Implement TRoE delete function for attributes | Medium |
| 2.3.3 | Service routine with deletion logic (deleteAll/datasetId/default) | Medium |

#### 2.4 PATCH /temporal/entities/{entityId}/attrs/{attrId}/{instanceId}

| Step | Description | Effort |
|------|-------------|--------|
| 2.4.1 | Implement instance lookup in TRoE | Medium |
| 2.4.2 | Partial update logic for attribute instances | High |
| 2.4.3 | Implement service routine | Medium |

#### 2.5 DELETE /temporal/entities/{entityId}/attrs/{attrId}/{instanceId}

| Step | Description | Effort |
|------|-------------|--------|
| 2.5.1 | TRoE delete function for individual instances | Medium |
| 2.5.2 | Implement service routine | Low |

---

### Phase 3: Advanced Features (Priority: Low)

| Feature | Description | Affected Endpoints |
|---------|-------------|-------------------|
| orderBy | Sort by entity member | GET collection |
| EntityMap | `entityMap`/`entityMapLifetime` | GET collection |
| Distributed Temporal | Forwarding to context sources | All |
| expandValues/jsonKeys | Special expansion | GET collection, POST query |

---

### Infrastructure Changes Summary

The following files need modifications for most implementations:

| File | Changes |
|------|---------|
| `src/lib/orionld/service/orionldServiceInit.cpp` | Remove `notImplemented` flags, register URI parameters |
| `src/lib/orionld/types/OrionLdRestService.h` | New URI parameter flags (deleteAll, datasetId, etc.) |
| `src/lib/orionld/common/orionldState.h` | New URI parameter fields in state |
| `src/lib/orionld/troe/` | New PostgreSQL functions for CRUD and extended queries |
| `src/app/orionld/orionldRestServices.cpp` | Routes remain as-is (already defined) |

### Existing Reusable Building Blocks

| Building Block | File | Reusable for |
|----------------|------|--------------|
| `timeColumnForTimeproperty()` | `pgTemporalEntityQuery.cpp` | All temporal queries |
| `attrsFilter()` | `pgTemporalEntityQuery.cpp` | All temporal queries |
| `lastN` window function | `pgTemporalEntityQuery.cpp` | All temporal queries |
| `pgTemporalEntityBuild()` | `pgTemporalEntityBuild.cpp` | All temporal responses |
| `temporalValuesTransform()` | `temporalValuesTransform.cpp` | All temporal responses |
| `aggregatedValuesTransform()` | `aggregatedValuesTransform.cpp` | All temporal responses |
| `pgTimestampToIso8601()` | `pgTemporalEntityBuild.cpp` | All temporal responses |
| `pgValueNodeBuild()` | `pgTemporalEntityBuild.cpp` | All temporal responses |
| `troeQStringToSql()` | `qTreeToSql.cpp` | GET collection, POST query |
| `geoFilterToSql()` | `geoFilterToSql.cpp` | GET collection, POST query |
| `troePostEntities()` | `troePostEntities.cpp` | POST attrs (analogous) |
