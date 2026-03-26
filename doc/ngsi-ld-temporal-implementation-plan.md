# NGSI-LD Temporal API - Implementierungsplan

> Basierend auf: ETSI GS CIM 009 V1.9.1, OpenAPI v1.8.1, Orion-LD Quellcode (Stand: Maerz 2026)

## Inhaltsverzeichnis

- [1. Status-Uebersicht](#1-status-uebersicht)
- [2. Detaillierte Gap-Analyse](#2-detaillierte-gap-analyse)
- [3. Implementierungsplan](#3-implementierungsplan)

---

## 1. Status-Uebersicht

| # | Methode | Pfad | Status | Quelldatei |
|---|---------|------|--------|------------|
| 1 | POST | `/temporal/entities` | **Implementiert** (kleine Luecken) | `orionldPostTemporalEntities.cpp` |
| 2 | GET | `/temporal/entities` | **Implementiert** (q, Geo, Aggregation fehlt) | `orionldGetTemporalEntities.cpp` |
| 3 | GET | `/temporal/entities/{entityId}` | **Implementiert** (Aggregation fehlt) | `orionldGetTemporalEntity.cpp` |
| 4 | DELETE | `/temporal/entities/{entityId}` | **Nicht implementiert** (501-Stub) | `orionldDeleteTemporalEntity.cpp` |
| 5 | POST | `/temporal/entities/{entityId}/attrs` | **Nicht implementiert** (501-Stub) | `orionldPostTemporalAttributes.cpp` |
| 6 | DELETE | `.../attrs/{attrId}` | **Nicht implementiert** (501-Stub) | `orionldDeleteTemporalAttribute.cpp` |
| 7 | PATCH | `.../attrs/{attrId}/{instanceId}` | **Nicht implementiert** (501-Stub) | `orionldPatchTemporalAttributeInstance.cpp` |
| 8 | DELETE | `.../attrs/{attrId}/{instanceId}` | **Nicht implementiert** (501-Stub) | `orionldDeleteTemporalAttributeInstance.cpp` |
| 9 | POST | `/temporal/entityOperations/query` | **Implementiert** (q, Geo, Aggregation fehlt) | `orionldPostTemporalQuery.cpp` |

**Legende:**
- **Weitgehend implementiert** = Kernfunktionalitaet und die meisten Parameter vorhanden, einzelne Parameter fehlen noch
- **Implementiert (kleine Luecken)** = Funktional, ein FIXME im Code
- **Mintaka-Stub** = Gibt 501 zurueck mit Verweis auf Mintaka
- **501-Stub** = Gibt 501 "Not Implemented" zurueck

---

## 2. Detaillierte Gap-Analyse

### 2.1 POST /temporal/entities (orionldPostTemporalEntities.cpp)

**Status:** Implementiert mit kleinen Luecken

**Was funktioniert:**
- Entity-ID und Type werden validiert (`pCheckEntityId`, `pCheckEntityType`)
- Attribute werden per `pCheckAttribute` validiert und expandiert (Array und Object)
- TRoE-Datenbank wird via `troePostEntities()` befuellt
- Upsert-Logik: 201 bei neuer Entity, 204 bei bestehender (`mongocEntityLookup`)
- `Location`-Header bei 201 Created
- `local` URI-Parameter registriert (Zeile 523 in ServiceInit)

**Was fehlt:**

| Luecke | Prioritaet | Details |
|--------|-----------|---------|
| location/observationSpace/operationSpace | Mittel | FIXME im Code (Zeile 115): Spezial-Attribute werden nicht validiert/verarbeitet |
| Fehlercode 422 | Niedrig | Laut Spec soll 422 Unprocessable Entity zurueckgegeben werden wenn Operation nicht verfuegbar |

---

### 2.2 GET /temporal/entities (orionldGetTemporalEntities.cpp)

**Status:** Weitgehend implementiert (Kernfunktionalitaet)

**Was funktioniert:**
- Multi-Entity temporale Query ueber TRoE PostgreSQL
- Zwei-Phasen-Architektur: Entity-Discovery mit Paginierung, dann pro Entity pgTemporalEntityQuery/Build
- Entity-Filterung: `type`, `id` (Liste), `idPattern` (Regex)
- Temporale Filterung: `timerel` (before/after/between), `timeAt`, `endTimeAt`
- `timeproperty` (observedAt/modifiedAt/createdAt)
- `attrs`-Filterung (nur angeforderte Attribute)
- `lastN` - Letzte N Instanzen pro Attribut/Dataset
- Paginierung: `limit`, `offset`
- `count` - NGSILD-Results-Count Header
- `pick`/`omit` - Inklusions-/Exklusions-Projektion (mutual exclusivity validiert)
- `datasetId` - Dataset-Filterung (temporal-spezifisch, Arrays bleiben erhalten)
- `lang`, `format` (normalized/concise/simplified), `options` (sysAttrs, temporalValues)
- `local` URI-Parameter registriert
- Validierung: timerel+timeAt pflicht, type oder attrs pflicht, endTimeAt fuer between
- URI-Parameter in ServiceInit registriert: OPTIONS, FORMAT, LIMIT, OFFSET, COUNT, IDLIST, TYPELIST, IDPATTERN, ATTRS, LASTN, TIMEREL, TIMEAT, ENDTIMEAT, TIMEPROPERTY, PICK, OMIT, DATASETID, LOCAL, LANG

**Was noch fehlt:**

| Parameter | Prioritaet | Details |
|-----------|-----------|---------|
| `q` | Hoch | NGSI-LD Query Language - erfordert Q-Tree zu SQL Uebersetzung |
| `geometry`/`georel`/`coordinates`/`geoproperty` | Hoch | Geo-Queries - erfordert PostGIS-Integration |
| `aggrMethods`/`aggrPeriodDuration` | Mittel | Aggregation (avg, min, max, etc.) |
| `orderBy` | Mittel | Sortierung nach Entity-Member |
| `scopeQ` | Niedrig | Scope Query |
| `csf` | Niedrig | Context Source Filter |
| `expandValues`/`jsonKeys` | Niedrig | Spezial-Expansion |
| `collation` | Niedrig | ICU Collation |
| `splitEntities`/`entityMap`/`entityMapLifetime` | Niedrig | Verteilte Entities und EntityMap |

---

### 2.3 GET /temporal/entities/{entityId} (orionldGetTemporalEntity.cpp)

**Status:** Weitgehend implementiert

**Was funktioniert:**
- Entity-ID aus URL extrahiert und validiert (`pCheckUri`)
- `timerel` (before/after/between), `timeAt`, `endTimeAt` werden validiert
- `timeproperty` - `timeColumnForTimeproperty()` mappt auf SQL-Spalten (`observedat` fuer observedAt/default, `ts` fuer modifiedAt/createdAt)
- `attrs`-Filterung via `attrsFilter()` baut `AND id IN (...)` SQL-Clause
- `lastN` - Window-Function `ROW_NUMBER() OVER (PARTITION BY id, datasetid ORDER BY <timeCol>)` mit `WHERE rn <= lastN`
- `count` - `NGSILD-Results-Count`-Header via `orionldHeaderAdd()` (Zeile 233-234)
- PostgreSQL-Query via `pgTemporalEntityQuery()` mit 3 Resultsets (entity, attrs, subAttrs)
- Entity-Aufbau via `pgTemporalEntityBuild()` mit:
  - Alle Werttypen: String, Number, Boolean, DateTime, Compound, Relationship, LanguageMap, Geo*
  - `instanceId` pro Attribut-Instanz
  - `observedAt` mit PG-Timestamp -> ISO 8601 Konvertierung
  - `unitCode` und `datasetId`
  - Sub-Attribute mit Matching ueber `attrInstanceId`
- Ausgabeformate: simplified, concise, normalized
- `temporalValues`-Transformation: Array-of-instances -> `{ "type": "...", "values": [[val, ts], ...] }`
- `sysAttrs`-Option und `lang`-Parameter
- 404 wenn Entity nicht gefunden, 501 wenn TRoE nicht aktiviert
- Sortierung nach dynamischem `timeCol` statt hardcoded `ts`
- URI-Parameter in ServiceInit registriert: `OPTIONS`, `FORMAT`, `ATTRS`, `COUNT`, `LASTN`, `TIMEREL`, `TIMEAT`, `ENDTIMEAT`, `TIMEPROPERTY`, `PICK`, `OMIT`, `DATASETID`, `LOCAL`, `LANG`
- `pick`-Parameter: Post-Processing via `pickForEntity()` - filtert Attribute nach Namen
- `omit`-Parameter: Post-Processing via `omitForEntity()` - entfernt benannte Attribute
- `pick`/`omit` Mutual Exclusivity: 400-Fehler bei gleichzeitiger Verwendung
- `datasetId`-Filterung: `datasetTemporalEntityFix()` entfernt nicht-passende Instanzen (Arrays bleiben erhalten)
- `timerel`/`timeAt` optional: ohne diese Parameter werden alle Attribut-Instanzen zurueckgegeben

**Kuerzlich implementiert:**

| Parameter | Status | Details |
|-----------|--------|---------|
| `pick` | **Erledigt** | Post-Processing via `pickForEntity()` nach Kompaktierung |
| `omit` | **Erledigt** | Neues `omit`-Infrastruktur (Flag, Parsing, `omitForEntity()`) |
| `datasetId` | **Erledigt** | `datasetTemporalEntityFix()` - temporale Variante ohne Array-Flattening |
| Optional `timerel`/`timeAt` | **Erledigt** | Beide optional per Spec Clause 6.19.3.1 - ohne = alle Instanzen |
| `pick`/`omit` Mutual Exclusivity | **Erledigt** | 400-Fehler wenn beide gleichzeitig angegeben |

**Was noch fehlt:**

| Parameter | Prioritaet | Aktueller Stand |
|-----------|-----------|-----------------|
| `aggrMethods` | Mittel | Nicht implementiert - keine Aggregationslogik |
| `aggrPeriodDuration` | Mittel | Nicht implementiert |
| `options` (aggregatedValues) | Mittel | Nicht implementiert (nur `temporalValues` und `sysAttrs`) |
| `local` | Niedrig | URI-Param registriert, aber keine Logik dahinter |

---

### 2.4 DELETE /temporal/entities/{entityId} (orionldDeleteTemporalEntity.cpp)

**Status:** 501-Stub

**Zu implementieren:**
- Entity-ID aus URL extrahieren und validieren
- Pruefen ob Entity in TRoE existiert (404 wenn nicht)
- Alle temporalen Daten aus PostgreSQL (TRoE) loeschen: entities, attributes, sub-attributes Tabellen
- Optional: Auch aus MongoDB loeschen (abhaengig von Architektur-Entscheidung)
- 204 No Content bei Erfolg

**Betroffene Dateien:**
- `src/lib/orionld/serviceRoutines/orionldDeleteTemporalEntity.cpp` - Hauptlogik
- `src/lib/orionld/troe/` - Neue Funktion `pgTemporalEntityDelete()` oder aehnlich
- `src/lib/orionld/service/orionldServiceInit.cpp` - `notImplemented = true` entfernen (Zeile 548)

---

### 2.5 POST /temporal/entities/{entityId}/attrs (orionldPostTemporalAttributes.cpp)

**Status:** 501-Stub

**Zu implementieren:**
- Entity-ID aus URL extrahieren und validieren
- Pruefen ob Entity existiert (404 wenn nicht)
- Request Body als `EntityTemporalFragment` parsen und validieren
- Attribute via `pCheckAttribute` validieren und expandieren
- Neue Attribut-Instanzen in TRoE einfuegen
- 204 No Content bei Erfolg

**Betroffene Dateien:**
- `src/lib/orionld/serviceRoutines/orionldPostTemporalAttributes.cpp` - Hauptlogik
- `src/lib/orionld/troe/` - Neue/erweiterte TRoE-Funktionen
- `src/lib/orionld/service/orionldServiceInit.cpp` - `notImplemented = true` entfernen (Zeile 552)

---

### 2.6 DELETE /temporal/entities/{entityId}/attrs/{attrId} (orionldDeleteTemporalAttribute.cpp)

**Status:** 501-Stub

**Zu implementieren:**
- `entityId` und `attrId` aus URL extrahieren und validieren
- URL-Parameter `datasetId` und `deleteAll` parsen
- Pruefen ob Entity und Attribut existieren (404 wenn nicht)
- Loeschlogik:
  - `deleteAll=true`: Alle Instanzen des Attributs loeschen
  - `datasetId` angegeben: Nur das spezifische Dataset loeschen
  - Weder noch: Default-Attribut-Instanz loeschen
- Attribut-Daten aus TRoE PostgreSQL loeschen (attributes + sub-attributes)
- 204 No Content bei Erfolg

**Betroffene Dateien:**
- `src/lib/orionld/serviceRoutines/orionldDeleteTemporalAttribute.cpp` - Hauptlogik
- `src/lib/orionld/troe/` - Neue Funktion fuer Attribut-Loeschung
- `src/lib/orionld/service/orionldServiceInit.cpp` - `notImplemented = true` entfernen (Zeile 544)
- `src/lib/orionld/types/OrionLdRestService.h` - ggf. neue URI-Parameter-Flags fuer `deleteAll`, `datasetId`

---

### 2.7 PATCH /temporal/entities/{entityId}/attrs/{attrId}/{instanceId} (orionldPatchTemporalAttributeInstance.cpp)

**Status:** 501-Stub

**Zu implementieren:**
- `entityId`, `attrId`, `instanceId` aus URL extrahieren und validieren
- Request Body als `EntityTemporalFragment` parsen
- Pruefen ob Entity, Attribut und Instanz existieren (404 wenn nicht)
- Partielle Aktualisierung der Attribut-Instanz in TRoE
- 204 No Content bei Erfolg

**Betroffene Dateien:**
- `src/lib/orionld/serviceRoutines/orionldPatchTemporalAttributeInstance.cpp` - Hauptlogik
- `src/lib/orionld/troe/` - Neue Funktion fuer Instanz-Update
- `src/lib/orionld/service/orionldServiceInit.cpp` - `notImplemented = true` entfernen (Zeile 550)

---

### 2.8 DELETE /temporal/entities/{entityId}/attrs/{attrId}/{instanceId} (orionldDeleteTemporalAttributeInstance.cpp)

**Status:** 501-Stub

**Zu implementieren:**
- `entityId`, `attrId`, `instanceId` aus URL extrahieren und validieren
- Pruefen ob Entity, Attribut und Instanz existieren (404 wenn nicht)
- Einzelne Attribut-Instanz aus TRoE loeschen
- 204 No Content bei Erfolg

**Betroffene Dateien:**
- `src/lib/orionld/serviceRoutines/orionldDeleteTemporalAttributeInstance.cpp` - Hauptlogik
- `src/lib/orionld/troe/` - Neue Funktion fuer Instanz-Loeschung
- `src/lib/orionld/service/orionldServiceInit.cpp` - `notImplemented = true` entfernen (Zeile 546)

---

### 2.9 POST /temporal/entityOperations/query (orionldPostTemporalQuery.cpp)

**Status:** Implementiert (Kernfunktionalitaet)

**Implementiert:**
- Request Body als `Query` mit `temporalQ` Objekt parsen
- Entity-Selektoren (type, id, idPattern) aus `entities` Array extrahieren
- Attribute aus `attrs` Array extrahieren und expandieren
- `temporalQ` mit timerel, timeAt, endTimeAt, timeproperty parsen und validieren
- Zweiphasige Abfrage: Entity-Discovery via `pgTemporalEntitiesQuery`, dann pro Entity `pgTemporalEntityQuery` + `pgTemporalEntityBuild`
- Post-Processing: Compact, datasetId-Filter, Format-Transform, temporalValues, sysAttrs, pick/omit
- Pagination via URL-Parameter (limit, offset, count)
- 200 OK mit EntityTemporal[] Array

**Noch fehlend:**
- q-Parameter (NGSI-LD Query Language)
- geoQ (Geo-Queries via PostGIS)
- scopeQ
- Aggregation (aggrMethods, aggrPeriodDuration)

**Betroffene Dateien:**
- `src/lib/orionld/serviceRoutines/orionldPostTemporalQuery.cpp` - Hauptlogik (komplett neu implementiert)
- `src/lib/orionld/service/orionldServiceInit.cpp` - `mintaka = true` durch URI-Param-Registrierung ersetzt

---

## 3. Implementierungsplan

### Phase 1: Bestehende Endpunkte vervollstaendigen (Prioritaet: Hoch)

Vervollstaendigung der bereits funktionierenden Endpunkte.

#### 1.1 GET /temporal/entities/{entityId} - Fehlende Parameter

| Schritt | Beschreibung | Aufwand |
|---------|--------------|---------|
| 1.1.1 | `pick`/`omit`-Projektion nach Entity-Build anwenden | Mittel |
| 1.1.2 | `datasetId`-Filterung in SQL einbauen (`AND datasetid IN (...)`) | Mittel |
| 1.1.3 | `timerel`/`timeAt` optional machen (ohne = alle Instanzen zurueckgeben) | Niedrig |
| 1.1.4 | `aggrMethods`/`aggrPeriodDuration` - Aggregationslogik in SQL oder Post-Processing | Hoch |
| 1.1.5 | `local`-Parameter registrieren und verarbeiten | Niedrig |

**Kritische Dateien:**
- `src/lib/orionld/serviceRoutines/orionldGetTemporalEntity.cpp`
- `src/lib/orionld/troe/pgTemporalEntityQuery.cpp`
- `src/lib/orionld/troe/pgTemporalEntityBuild.cpp`
- `src/lib/orionld/service/orionldServiceInit.cpp` (URI-Parameter registrieren)

#### 1.2 POST /temporal/entities - FIXME beheben

| Schritt | Beschreibung | Aufwand |
|---------|--------------|---------|
| 1.2.1 | `location`, `observationSpace`, `operationSpace` Attribute erkennen und validieren | Mittel |

**Kritische Dateien:**
- `src/lib/orionld/serviceRoutines/orionldPostTemporalEntities.cpp`

---

### Phase 2: Einfache CRUD-Endpunkte (Prioritaet: Hoch)

Endpunkte die keine komplexen Queries erfordern sondern einfache DB-Operationen.

#### 2.1 DELETE /temporal/entities/{entityId}

| Schritt | Beschreibung | Aufwand |
|---------|--------------|---------|
| 2.1.1 | TRoE-Loeschfunktion `pgTemporalEntityDelete()` implementieren | Mittel |
| 2.1.2 | Service-Routine implementieren | Niedrig |
| 2.1.3 | `notImplemented`-Flag entfernen in ServiceInit | Trivial |

#### 2.2 POST /temporal/entities/{entityId}/attrs

| Schritt | Beschreibung | Aufwand |
|---------|--------------|---------|
| 2.2.1 | Attribut-Validierung und Expansion (analog zu POST entities) | Mittel |
| 2.2.2 | TRoE-Insert fuer neue Attribut-Instanzen | Mittel |
| 2.2.3 | Service-Routine implementieren | Mittel |

#### 2.3 DELETE /temporal/entities/{entityId}/attrs/{attrId}

| Schritt | Beschreibung | Aufwand |
|---------|--------------|---------|
| 2.3.1 | URL-Parameter `deleteAll` und `datasetId` registrieren | Niedrig |
| 2.3.2 | TRoE-Loeschfunktion fuer Attribute implementieren | Mittel |
| 2.3.3 | Service-Routine mit Loeschlogik (deleteAll/datasetId/default) | Mittel |

#### 2.4 PATCH /temporal/entities/{entityId}/attrs/{attrId}/{instanceId}

| Schritt | Beschreibung | Aufwand |
|---------|--------------|---------|
| 2.4.1 | Instanz-Lookup in TRoE implementieren | Mittel |
| 2.4.2 | Partielle Update-Logik fuer Attribut-Instanzen | Hoch |
| 2.4.3 | Service-Routine implementieren | Mittel |

#### 2.5 DELETE /temporal/entities/{entityId}/attrs/{attrId}/{instanceId}

| Schritt | Beschreibung | Aufwand |
|---------|--------------|---------|
| 2.5.1 | TRoE-Loeschfunktion fuer einzelne Instanzen | Mittel |
| 2.5.2 | Service-Routine implementieren | Niedrig |

---

### Phase 3: Komplexe Query-Endpunkte (Prioritaet: Mittel)

Diese erfordern umfangreiche SQL-Query-Generierung, Paginierung, Sortierung und Filterung.
Kernlogik aus `pgTemporalEntityQuery.cpp` (timeproperty, attrs, lastN, timerel) kann wiederverwendet werden.

#### 3.1 GET /temporal/entities

| Schritt | Beschreibung | Aufwand |
|---------|--------------|---------|
| 3.1.1 | Multi-Entity temporale Query in PostgreSQL (erweitere pgTemporalEntityQuery fuer Mehrfach-Entities) | Hoch |
| 3.1.2 | Entity-Typ/ID-Filterung (type, id, idPattern) | Mittel |
| 3.1.3 | NGSI-LD Query-Sprache (q-Parameter) gegen TRoE | Hoch |
| 3.1.4 | Geo-Query-Integration | Hoch |
| 3.1.5 | Paginierung (limit/offset) und Count | Mittel |
| 3.1.6 | Sortierung (orderBy) | Mittel |
| 3.1.7 | Scope-Query | Mittel |
| 3.1.8 | Wiederverwendung bestehender Parameter (timeproperty, attrs, lastN, lang, format, temporalValues) | Niedrig |
| 3.1.9 | `mintaka`-Flag entfernen in ServiceInit | Trivial |

#### 3.2 POST /temporal/entityOperations/query

| Schritt | Beschreibung | Aufwand |
|---------|--------------|---------|
| 3.2.1 | Request-Body-Parsing (Query + TemporalQuery Datentyp) | Mittel |
| 3.2.2 | Wiederverwendung der GET-Query-Logik | Niedrig |
| 3.2.3 | `mintaka`-Flag entfernen in ServiceInit | Trivial |

---

### Phase 4: Erweiterte Features (Prioritaet: Niedrig)

| Feature | Beschreibung | Betroffene Endpunkte |
|---------|--------------|---------------------|
| Aggregation | `aggrMethods`/`aggrPeriodDuration` (avg, min, max, sum, sumsq, stddev, distinctCount) | GET single, GET collection, POST query |
| EntityMap | `entityMap`/`entityMapLifetime` | GET collection |
| Distributed Temporal | Forwarding zu Context Sources | Alle |
| expandValues/jsonKeys | Spezial-Expansion | GET collection, POST query |

---

### Zusammenfassung der Infrastruktur-Aenderungen

Folgende Dateien muessen fuer fast alle Implementierungen angepasst werden:

| Datei | Aenderungen |
|-------|------------|
| `src/lib/orionld/service/orionldServiceInit.cpp` | `notImplemented`/`mintaka`-Flags entfernen, URI-Parameter registrieren |
| `src/lib/orionld/types/OrionLdRestService.h` | Neue URI-Parameter-Flags (deleteAll, datasetId, etc.) |
| `src/lib/orionld/common/orionldState.h` | Neue URI-Parameter-Felder im State |
| `src/lib/orionld/troe/` | Neue PostgreSQL-Funktionen fuer CRUD und erweiterte Queries |
| `src/app/orionld/orionldRestServices.cpp` | Routen bleiben bestehen (bereits definiert) |

### Bereits vorhandene wiederverwendbare Bausteine

| Baustein | Datei | Wiederverwendbar fuer |
|----------|-------|----------------------|
| `timeColumnForTimeproperty()` | `pgTemporalEntityQuery.cpp:54` | GET collection, POST query |
| `attrsFilter()` | `pgTemporalEntityQuery.cpp:77` | GET collection, POST query |
| `lastN` Window-Function | `pgTemporalEntityQuery.cpp:174-178` | GET collection, POST query |
| `pgTemporalEntityBuild()` | `pgTemporalEntityBuild.cpp:239` | GET collection (pro Entity) |
| `temporalValuesTransform()` | `orionldGetTemporalEntity.cpp:64` | GET collection, POST query |
| `pgTimestampToIso8601()` | `pgTemporalEntityBuild.cpp:47` | Alle temporalen Responses |
| `pgValueNodeBuild()` | `pgTemporalEntityBuild.cpp:108` | Alle temporalen Responses |
| `troePostEntities()` | `troe/troePostEntities.cpp` | POST attrs (analog) |
