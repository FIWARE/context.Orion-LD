# GEOS Integration for Geofencing in Subscription Matching

## Background

Orion-LD currently does **no geofencing** in the subscription notification path. When an entity is modified, the broker checks subscriptions against entity type, entity id, attributes, and `q` filter — but the `geoQ` filter is skipped entirely. The code comment in `subCacheAlterationMatch.cpp:529` says:

```
// 'geoQ' MUST come last as it requires a database query
// (OR: somehow use GEOS library and fix it that way ...)
```

The GEOS library eliminates the need for a database query by performing geo-predicates in-process.

## GEOS Library

- **Repository:** https://github.com/libgeos/geos
- **Language:** C++ with a stable C API (`geos_c.h`)
- **License:** LGPL 2.1
- **Maturity:** 20+ years, used by PostGIS, QGIS, Shapely, etc.

### Key Features for Orion-LD

| Feature | Why it matters |
|---|---|
| `GEOSGeoJSONReader_readGeometry()` | Native GeoJSON parsing — NGSI-LD geometries go in directly |
| `GEOSPreparedGeometry` | Pre-index a subscription's reference geometry once, match many entity locations against it in O(log n) |
| Thread-safe `_r` API | Every function has a reentrant variant with `GEOSContextHandle_t` — fits Orion-LD's threaded architecture |
| `GEOSDistanceWithin()` | Short-circuits distance calculation for `near` — returns as soon as it finds points closer than threshold |
| STRtree (R-tree) | Spatial index for bulk queries, useful if subscription count grows large |

## NGSI-LD Georel Coverage

From ETSI GS CIM 009 V1.9.1, section 4.10 — all seven georel predicates map directly to GEOS:

| NGSI-LD georel | GEOS function | Prepared (fast) variant |
|---|---|---|
| `near` (maxDistance) | `GEOSDistanceWithin` | `GEOSPreparedDistanceWithin` |
| `near` (minDistance) | `!GEOSDistanceWithin` | `GEOSPreparedDistance` + compare |
| `within` | `GEOSWithin` | `GEOSPreparedWithin` |
| `contains` | `GEOSContains` | `GEOSPreparedContains` |
| `intersects` | `GEOSIntersects` | `GEOSPreparedIntersects` |
| `equals` | `GEOSEquals` | — |
| `disjoint` | `GEOSDisjoint` | `GEOSPreparedDisjoint` |
| `overlaps` | `GEOSOverlaps` | `GEOSPreparedOverlaps` |

### Geometry Types

All GeoJSON types supported by NGSI-LD (all except GeometryCollection):
- Point, MultiPoint
- LineString, MultiLineString
- Polygon, MultiPolygon

## Insertion Point in the Matching Loop

File: `src/lib/orionld/notifications/subCacheAlterationMatch.cpp`

Current check order:
1. Tenant
2. Subscription status (active, expired, throttled)
3. Entity type & entity id
4. `q` filter
5. Attribute match → builds matchList

Geo-match goes **after attribute match, before adding to matchList** — it's the most expensive check so it runs last, only on entities that already passed all cheaper filters.

## Altitude (Z coordinate)

GEOS stores 3D coordinates but **ignores Z in all predicates** — same behavior as MongoDB's geospatial operators. This is consistent with the NGSI-LD spec, which defines no altitude semantics for georel predicates.

When altitude filtering is needed (drones, building floors), a simple Z-range post-filter can be added on top:

```c
if (geos2dMatch && entity.z >= fence.minAltitude && entity.z <= fence.maxAltitude)
```

This applies equally to both the GEOS path (subscription matching) and the MongoDB path (GET with geoQ).

## Cartesian vs Geodetic

GEOS operates in Cartesian coordinates. For NGSI-LD geo-predicates:

- **Topological predicates** (`within`, `contains`, `intersects`, `equals`, `disjoint`, `overlaps`): Cartesian is correct — a point inside a polygon is inside regardless of coordinate system.
- **`near` with meters**: Cartesian distance between longitude/latitude values is not meters. Use a haversine function (~15 lines) instead of `GEOSDistance` for the `near` georel.

## Implementation Plan

### Tasks

| Task | Effort |
|---|---|
| Build system — add GEOS dependency (CMake find/link `libgeos_c`) | ~30 min |
| PreparedGeometry caching in `CachedSubscription` — build at subscription creation/cache-load, destroy on eviction | ~1-2 hours |
| `geoMatch()` function — dispatch on georel, all 7 predicates | ~2-3 hours |
| GeoJSON extraction from entity's KjNode tree (geoproperty lookup, serialize to GeoJSON string for GEOS reader) | ~1 hour |
| Integration in `subCacheAlterationMatch` after `attributeMatch` | ~30 min |
| Pernot subscription geo-matching (already has `geoSelector` field in `PernotSubscription`) | ~1 hour |
| Haversine for `near` georel | ~30 min |
| Cleanup/lifecycle (destroy GEOS geometries on sub cache eviction, process exit) | ~30 min |
| ~40 functional tests | ~4-6 hours |
| **Total** | **~10-14 hours** |

### Test Matrix (~40 tests)

- 7 georel types × 3 geometry types (Point, Polygon, LineString) = ~21 basic tests
- `near` with maxDistance / minDistance / both = ~6 tests
- Custom geoproperty (not `location`) = ~3 tests
- Edge cases: entity without geoproperty, geo + q combined, MultiPolygon = ~5 tests
- Subscription CRUD with geoQ = ~5 tests

### Why Not Reimplement Instead of Using GEOS?

Custom implementation: **~12-16 days** (point-in-polygon, segment intersection, all geometry combinations, robustness/edge cases, numerical precision bugs).

Performance difference: **negligible**. The hot path is one predicate call per entity-subscription pair — microseconds either way. GEOS's `PreparedGeometry` actually gives a performance advantage (edge lookup trees, O(log n)) that you'd have to reimplement yourself.

## Files to Modify

| File | Change |
|---|---|
| `CMakeLists.txt` (or equivalent) | Add `libgeos_c` dependency |
| `src/lib/cache/CachedSubscription.h` | Add `GEOSPreparedGeometry*` field |
| `src/lib/orionld/notifications/subCacheAlterationMatch.cpp` | Add `geoMatch()` call after `attributeMatch` |
| `src/lib/orionld/notifications/geoMatch.cpp` (new) | The geo-matching function |
| `src/lib/orionld/pernot/pernotTreat.cpp` (or equivalent) | Add geo-match for pernot subscriptions |
| Sub-cache init/refresh code | Build `GEOSPreparedGeometry` when loading subscriptions |
| Sub-cache cleanup code | Destroy GEOS geometries |
