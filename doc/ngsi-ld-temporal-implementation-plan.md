# NGSI-LD Temporal API - Implementierungsplan

> Basierend auf: ETSI GS CIM 009 V1.9.1, OpenAPI v1.8.1, Orion-LD Quellcode (Stand: Maerz 2025)

## Inhaltsverzeichnis

- [1. Status-Uebersicht](#1-status-uebersicht)
- [2. Detaillierte Gap-Analyse](#2-detaillierte-gap-analyse)
- [3. Implementierungsplan](#3-implementierungsplan)

---

## 1. Status-Uebersicht

| # | Methode | Pfad | Status | Quelldatei |
|---|---------|------|--------|------------|
| 1 | POST | `/temporal/entities` | **Implementiert** (Luecken) | `orionldPostTemporalEntities.cpp` |
| 2 | GET | `/temporal/entities` | **Nicht implementiert** (Mintaka-Stub) | `orionldGetTemporalEntities.cpp` |
| 3 | GET | `/temporal/entities/{entityId}` | **Implementiert** (Luecken) | `orionldGetTemporalEntity.cpp` |
| 4 | DELETE | `/temporal/entities/{entityId}` | **Nicht implementiert** (501-Stub) | `orionldDeleteTemporalEntity.cpp` |
| 5 | POST | `/temporal/entities/{entityId}/attrs` | **Nicht implementiert** (501-Stub) | `orionldPostTemporalAttributes.cpp` |
| 6 | DELETE | `.../attrs/{attrId}` | **Nicht implementiert** (501-Stub) | `orionldDeleteTemporalAttribute.cpp` |
| 7 | PATCH | `.../attrs/{attrId}/{instanceId}` | **Nicht implementiert** (501-Stub) | `orionldPatchTemporalAttributeInstance.cpp` |
| 8 | DELETE | `.../attrs/{attrId}/{instanceId}` | **Nicht implementiert** (501-Stub) | `orionldDeleteTemporalAttributeInstance.cpp` |
| 9 | POST | `/temporal/entityOperations/query` | **Nicht implementiert** (Mintaka-Stub) | `orionldPostTemporalQuery.cpp` |

**Legende:**
- **Implementiert (Luecken)** = Grundfunktionalitaet vorhanden, aber fehlende Parameter/Features
- **Mintaka-Stub** = Gibt 501 zurueck mit Verweis auf Mintaka
- **501-Stub** = Gibt 501 "Not Implemented" zurueck

---

## 2. Detaillierte Gap-Analyse

### 2.1 POST /temporal/entities (orionldPostTemporalEntities.cpp)

**Status:** Grundfunktionalitaet implementiert

**Was funktioniert:**
- Entity-ID und Type werden validiert (`pCheckEntityId`, `pCheckEntityType`)
- Attribute werden per `pCheckAttribute` validiert und expandiert
- TRoE-Datenbank wird via `troePostEntities()` befuellt
- Upsert-Logik: 201 bei neuer Entity, 204 bei bestehender (`mongocEntityLookup`)
- `Location`-Header bei 201 Created

**Was fehlt:**

| Luecke | Prioritaet | Details |
|--------|-----------|---------|
| location/observationSpace/operationSpace | Hoch | FIXME im Code (Zeile 115): Spezial-Attribute werden nicht validiert/verarbeitet |
| Fehlercode 422 | Niedrig | Laut Spec soll 422 Unprocessable Entity zurueckgegeben werden wenn Operation nicht verfuegbar |

---

### 2.2 GET /temporal/entities (orionldGetTemporalEntities.cpp)

**Status:** Nicht implementiert - Mintaka-Stub (501)

**Was fehlt (komplett):**

Alle URL-Parameter muessen implementiert werden:

| Parameter | Typ | Pflicht | Details |
|-----------|-----|---------|---------|
| `id` | Comma-separated URIs | Nein | Entity-ID-Filter |
| `idPattern` | Regex | Nein | ID-Muster |
| `type` | String | Bedingt | Entity-Typ (Pflicht wenn `attrs` fehlt) |
| `attrs` | Comma-separated | Bedingt | Attribut-Selektion |
| `pick` | Comma-separated | Nein | Inklusions-Projektion |
| `omit` | Comma-separated | Nein | Exklusions-Projektion |
| `q` | String | Nein | NGSI-LD Query |
| `csf` | String | Nein | Context Source Filter |
| `geometry` | String | Bedingt | Geo-Query |
| `georel` | String | Bedingt | Geo-Relationship |
| `coordinates` | String | Bedingt | Koordinaten |
| `geoproperty` | String | Nein | GeoProperty-Name |
| `timerel` | String | **Ja** | Temporale Beziehung |
| `timeAt` | DateTime | **Ja** | Vergleichszeitpunkt |
| `endTimeAt` | DateTime | Bedingt | Endzeitpunkt |
| `timeproperty` | String | Nein | Temporal Property |
| `lastN` | Integer | Nein | Letzte N Instanzen |
| `lang` | String | Nein | Sprache |
| `aggrMethods` | Comma-separated | Bedingt | Aggregation |
| `aggrPeriodDuration` | String | Nein | Aggregationsperiode |
| `scopeQ` | String | Nein | Scope Query |
| `datasetId` | Comma-separated URIs | Nein | Dataset-Filter |
| `expandValues` | Comma-separated | Nein | Werte-Expansion |
| `jsonKeys` | Comma-separated | Nein | JSON-Key-Filter |
| `orderBy` | String | Nein | Sortierung |
| `collation` | String | Nein | Collation |
| `splitEntities` | Boolean | Nein | Verteilte Entities |
| `entityMap` | Boolean | Nein | EntityMap |
| `entityMapLifetime` | String | Nein | EntityMap-Lebensdauer |
| `limit` | Integer | Nein | Paginierung |
| `count` | Boolean | Nein | Ergebniszaehler |
| `options` | String | Nein | sysAttrs, aggregatedValues, temporalValues |
| `format` | String | Nein | normalized, concise, simplified, aggregated |
| `local` | Boolean | Nein | Nur lokal |

**Bemerkung:** Dies ist der komplexeste Endpunkt. Eine PostgreSQL-basierte Implementierung muss temporale Queries ueber mehrere Entities ausfuehren und das Ergebnis mit Paginierung, Sortierung, Geo-Filterung und Aggregation aufbereiten.

---

### 2.3 GET /temporal/entities/{entityId} (orionldGetTemporalEntity.cpp)

**Status:** Weitgehend implementiert (auf Branch `claude/review-temporal-api-feedback-i4bC1`)

> **Hinweis:** Der aktuelle `develop`-Branch enthaelt eine aeltere Version. Der Branch
> `claude/review-temporal-api-feedback-i4bC1` (Commit `1ea41bb` ff.) hat wesentliche Erweiterungen
> die noch nicht gemerged wurden.

**Was funktioniert (develop):**
- Entity-ID aus URL extrahiert und validiert
- `timerel` (before/after/between), `timeAt`, `endTimeAt` werden validiert
- PostgreSQL-Query via `pgTemporalEntityQuery()` mit 3 Resultsets (entity, attrs, subAttrs)
- Entity-Aufbau via `pgTemporalEntityBuild()`
- Ausgabeformate: simplified, concise, normalized
- `sysAttrs`-Option und `lang`-Parameter
- 404 wenn Entity nicht gefunden

**Zusaetzlich implementiert (Branch `claude/review-temporal-api-feedback-i4bC1`):**
- `timeproperty` - `timeColumnForTimeproperty()` mappt auf SQL-Spalten (`observedat` fuer observedAt/default, `ts` fuer modifiedAt/createdAt)
- `attrs`-Filterung - `attrsFilter()` baut `AND id IN (...)` SQL-Clause
- `lastN` - Window-Function `ROW_NUMBER() OVER (PARTITION BY id, datasetid ORDER BY <timeCol>)` mit `WHERE rn <= lastN`
- `instanceId` in der SELECT-Liste
- `temporalValues` als gueltige Format-Option
- Sortierung nach `timeCol` statt hardcoded `ts`
- Test: `troe_get_temporal_entity_sort_timeproperty.test`

**Was fehlt (auch auf dem Review-Branch):**

| Parameter | Prioritaet | Aktueller Stand |
|-----------|-----------|-----------------|
| `pick` | Mittel | Nicht implementiert (nur `attrs` via SQL-Filter) |
| `omit` | Mittel | Nicht implementiert |
| `aggrMethods` | Mittel | Nicht implementiert - keine Aggregationslogik |
| `aggrPeriodDuration` | Mittel | Nicht implementiert |
| `datasetId` | Mittel | Nicht implementiert - kein Dataset-Filtering |
| `options` (aggregatedValues) | Mittel | Nicht implementiert |
| `local` | Niedrig | Nicht implementiert |

**Hinweis zur Implementierung:** `timerel` und `timeAt` sind laut Spec fuer diesen Endpunkt optional (anders als bei GET collection). Aktuell werden sie als Pflicht validiert (Zeilen 76-86), was nicht Spec-konform ist.

---

### 2.4 DELETE /temporal/entities/{entityId} (orionldDeleteTemporalEntity.cpp)

**Status:** 501-Stub

**Zu implementieren:**
- Entity-ID aus URL extrahieren und validieren
- Pruefen ob Entity existiert (404 wenn nicht)
- Alle temporalen Daten aus PostgreSQL (TRoE) loeschen: entities, attributes, sub-attributes Tabellen
- Optional: Auch aus MongoDB loeschen (abhaengig von Architektur-Entscheidung)
- 204 No Content bei Erfolg

**Betroffene Dateien:**
- `src/lib/orionld/serviceRoutines/orionldDeleteTemporalEntity.cpp` - Hauptlogik
- `src/lib/orionld/troe/` - Neue Funktion `pgTemporalEntityDelete()` oder aehnlich
- `src/lib/orionld/service/orionldServiceInit.cpp` - `notImplemented = true` entfernen (Zeile 536)

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
- `src/lib/orionld/service/orionldServiceInit.cpp` - `notImplemented = true` entfernen (Zeile 540)

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
- `src/lib/orionld/service/orionldServiceInit.cpp` - `notImplemented = true` entfernen (Zeile 532)
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
- `src/lib/orionld/service/orionldServiceInit.cpp` - `notImplemented = true` entfernen (Zeile 538)

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
- `src/lib/orionld/service/orionldServiceInit.cpp` - `notImplemented = true` entfernen (Zeile 534)

---

### 2.9 POST /temporal/entityOperations/query (orionldPostTemporalQuery.cpp)

**Status:** Mintaka-Stub (501)

**Zu implementieren:**
- Request Body als `Query` mit `TemporalQuery` parsen
- Alle Filter aus dem Body extrahieren (entities, attrs, q, geoQ, temporalQ, scopeQ)
- Temporale Multi-Entity-Query gegen TRoE ausfuehren
- Ergebnis als EntityTemporal[] zurueckgeben
- 200 OK bei Erfolg

**Bemerkung:** Dieser Endpunkt ist funktional aequivalent zu GET /temporal/entities, nur mit POST-Body statt URL-Parametern. Sollte nach der GET-Implementierung relativ einfach umgesetzt werden koennen.

**Betroffene Dateien:**
- `src/lib/orionld/serviceRoutines/orionldPostTemporalQuery.cpp` - Hauptlogik
- `src/lib/orionld/service/orionldServiceInit.cpp` - `mintaka = true` entfernen (Zeile 530)

---

## 3. Implementierungsplan

### Phase 1: Bestehende Endpunkte vervollstaendigen (Prioritaet: Hoch)

Vervollstaendigung der bereits funktionierenden Endpunkte.

#### 1.1 GET /temporal/entities/{entityId} - Fehlende Parameter

> **Voraussetzung:** Branch `claude/review-temporal-api-feedback-i4bC1` muss zuerst in `develop` gemerged werden.
> Dieser Branch liefert bereits: `timeproperty`, `attrs`-Filter, `lastN` (Window-Function), `instanceId`, `temporalValues`-Format.

| Schritt | Beschreibung | Aufwand |
|---------|--------------|---------|
| 1.1.1 | ~~`timeproperty`-Support~~ **Bereits implementiert** (Review-Branch) | Erledigt |
| 1.1.2 | ~~`attrs`-Filterung~~ **Bereits implementiert** (Review-Branch, SQL `AND id IN (...)`) | Erledigt |
| 1.1.3 | ~~`lastN`~~ **Bereits implementiert** (Review-Branch, Window-Function `ROW_NUMBER()`) | Erledigt |
| 1.1.4 | `pick`/`omit`-Projektion nach Entity-Build anwenden | Mittel |
| 1.1.5 | `datasetId`-Filterung in SQL einbauen | Mittel |
| 1.1.6 | `timerel`/`timeAt` optional machen (laut Spec nicht Pflicht fuer diesen Endpunkt) | Niedrig |
| 1.1.7 | `aggrMethods`/`aggrPeriodDuration` - Aggregationslogik | Hoch |
| 1.1.8 | `local`-Parameter | Niedrig |

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

#### 3.1 GET /temporal/entities

| Schritt | Beschreibung | Aufwand |
|---------|--------------|---------|
| 3.1.1 | Multi-Entity temporale Query in PostgreSQL | Hoch |
| 3.1.2 | Entity-Typ/ID-Filterung | Mittel |
| 3.1.3 | NGSI-LD Query-Sprache (q-Parameter) gegen TRoE | Hoch |
| 3.1.4 | Geo-Query-Integration | Hoch |
| 3.1.5 | Paginierung (limit/offset) und Count | Mittel |
| 3.1.6 | Sortierung (orderBy) | Mittel |
| 3.1.7 | Scope-Query | Mittel |
| 3.1.8 | Alle optionalen Parameter (lastN, lang, aggr, etc.) | Mittel |
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
| Aggregation | `aggrMethods`/`aggrPeriodDuration` | GET single, GET collection, POST query |
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
