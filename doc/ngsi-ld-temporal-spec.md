# NGSI-LD Temporal API - Spezifikationsreferenz

> Quellen: ETSI GS CIM 009 V1.9.1 (2025-07), NGSI-LD OpenAPI v1.8.1

## Inhaltsverzeichnis

- [1. Uebersicht](#1-uebersicht)
- [2. Temporal Query Language (Clause 4.11)](#2-temporal-query-language-clause-411)
- [3. Datentypen](#3-datentypen)
- [4. Endpunkte](#4-endpunkte)
  - [4.1 POST /temporal/entities](#41-post-temporalentities)
  - [4.2 GET /temporal/entities](#42-get-temporalentities)
  - [4.3 GET /temporal/entities/{entityId}](#43-get-temporalentitiesentityid)
  - [4.4 DELETE /temporal/entities/{entityId}](#44-delete-temporalentitiesentityid)
  - [4.5 POST /temporal/entities/{entityId}/attrs](#45-post-temporalentitiesentityidattrs)
  - [4.6 DELETE /temporal/entities/{entityId}/attrs/{attrId}](#46-delete-temporalentitiesentityidattrsattrid)
  - [4.7 PATCH /temporal/entities/{entityId}/attrs/{attrId}/{instanceId}](#47-patch-temporalentitiesentityidattrsattridinstanceid)
  - [4.8 DELETE /temporal/entities/{entityId}/attrs/{attrId}/{instanceId}](#48-delete-temporalentitiesentityidattrsattridinstanceid)
  - [4.9 POST /temporal/entityOperations/query](#49-post-temporalentityoperationsquery)
- [5. Gemeinsame HTTP-Header](#5-gemeinsame-http-header)

---

## 1. Uebersicht

Die NGSI-LD Temporal API ist ein optionaler Bestandteil der NGSI-LD API und verwaltet die zeitliche Entwicklung (Temporal Evolution) von Entities. Sie ist unterteilt in:

- **Temporal Context Information Provision** (Clause 5.6.11-5.6.16): Operationen zum Bereitstellen und Verwalten temporaler Daten
- **Temporal Context Information Consumption** (Clause 5.7.3-5.7.4): Operationen zum Abfragen temporaler Daten

### Endpunkt-Uebersicht

| # | Methode | Pfad | Operation | Spec-Clause | REST-Clause |
|---|---------|------|-----------|-------------|-------------|
| 1 | POST | `/temporal/entities` | Upsert Temporal Evolution | 5.6.11 | 6.18.3.1 |
| 2 | GET | `/temporal/entities` | Query Temporal Evolution | 5.7.4 | 6.18.3.2 |
| 3 | GET | `/temporal/entities/{entityId}` | Retrieve Temporal Evolution | 5.7.3 | 6.19.3.1 |
| 4 | DELETE | `/temporal/entities/{entityId}` | Delete Temporal Evolution | 5.6.16 | 6.19.3.2 |
| 5 | POST | `/temporal/entities/{entityId}/attrs` | Add Attributes to Temporal | 5.6.12 | 6.20.3.1 |
| 6 | DELETE | `/temporal/entities/{entityId}/attrs/{attrId}` | Delete Attribute from Temporal | 5.6.13 | 6.21.3.1 |
| 7 | PATCH | `/temporal/entities/{entityId}/attrs/{attrId}/{instanceId}` | Modify Attribute Instance | 5.6.14 | 6.22.3.1 |
| 8 | DELETE | `/temporal/entities/{entityId}/attrs/{attrId}/{instanceId}` | Delete Attribute Instance | 5.6.15 | 6.22.3.2 |
| 9 | POST | `/temporal/entityOperations/query` | Query Temporal via POST | 5.7.4 | 6.24.3.1 |

---

## 2. Temporal Query Language (Clause 4.11)

Die Temporal Query Language definiert Praedikate zum Filtern von Attribut-Instanzen basierend auf ihren Temporal Properties.

### Parameter

| Parameter | Typ | Pflicht | Beschreibung |
|-----------|-----|---------|--------------|
| `timerel` | String | Ja | Temporale Beziehung: `before`, `after`, `between` |
| `timeAt` | DateTime (ISO 8601) | Ja | Vergleichszeitpunkt |
| `endTimeAt` | DateTime (ISO 8601) | Nur bei `between` | Endzeitpunkt fuer Bereichsabfragen |
| `timeproperty` | String | Nein | Temporal Property fuer die Query. Erlaubte Werte: `observedAt` (default), `createdAt`, `modifiedAt`, `deletedAt` |

### Semantik

- **`before`**: Die angegebene Temporal Property muss **vor** `timeAt` liegen. Die Abfrage liefert Instanzen im Intervall `]TS, timeAt]` (wobei TS der Beginn der Zeitreihe ist).
- **`after`**: Die angegebene Temporal Property muss **nach** `timeAt` liegen. Die Abfrage liefert Instanzen im Intervall `[timeAt, TE[` (wobei TE das Ende der Zeitreihe ist).
- **`between`**: Die angegebene Temporal Property muss **zwischen** `timeAt` und `endTimeAt` liegen. Intervall: `[timeAt, endTimeAt]`. Der untere Grenzwert muss kleiner oder gleich dem oberen sein.

### Beispiele

```
# Alle Instanzen vor einem Zeitpunkt
?timerel=before&timeAt=2017-12-13T14:20:00Z

# Instanzen in einem Zeitraum, basierend auf modifiedAt
?timerel=between&timeAt=2017-12-13T14:20:00Z&endTimeAt=2017-12-13T14:40:00Z&timeproperty=modifiedAt

# Instanzen nach einem Zeitpunkt, basierend auf observedAt (default)
?timerel=after&timeAt=2017-12-13T14:20:00Z
```

---

## 3. Datentypen

### 3.1 EntityTemporal (Clause 5.2.4 / 5.2.20)

Repraesentiert die temporale Entwicklung einer Entity. Identisch mit dem Entity-Datentyp, jedoch werden Properties und Relationships in ihrer temporalen Form dargestellt (Arrays von Attribut-Instanzen).

```json
{
  "@context": "https://uri.etsi.org/ngsi-ld/v1/ngsi-ld-core-context-v1.8.jsonld",
  "id": "urn:ngsi-ld:Vehicle:A4567",
  "type": "Vehicle",
  "speed": [
    {
      "type": "Property",
      "value": 120,
      "observedAt": "2021-04-20T08:31:00Z",
      "instanceId": "urn:ngsi-ld:attribute:instance:1"
    },
    {
      "type": "Property",
      "value": 80,
      "observedAt": "2021-04-20T08:32:00Z",
      "instanceId": "urn:ngsi-ld:attribute:instance:2"
    }
  ]
}
```

### 3.2 EntityTemporalFragment

Fragment einer temporalen Entity-Repraesentation. Wird fuer Teilaktualisierungen verwendet (POST attrs, PATCH instance).

### 3.3 TemporalQuery (Clause 5.2.21)

Datentyp fuer temporale Abfragen (verwendet in POST /temporal/entityOperations/query).

| Feld | Typ | Kardinalitaet | Beschreibung |
|------|-----|---------------|--------------|
| `timeAt` | DateTime | 1 | Vergleichszeitpunkt (Clause 4.11) |
| `timerel` | String | 1 | Temporale Beziehung: `before`, `after`, `between` |
| `endTimeAt` | DateTime | 0..1 | Endzeitpunkt (Pflicht bei `between`) |
| `timeproperty` | String | 0..1 | Temporal Property: `observedAt` (default), `createdAt`, `modifiedAt`, `deletedAt` |
| `lastN` | Positive Integer | 0..1 | Nur die letzten N Instanzen pro Attribut/Entity |
| `aggrMethods` | Comma-separated Strings | 0..1 | Aggregationsmethoden (Pflicht wenn `aggregatedValues` in options) |
| `aggrPeriodDuration` | String (ISO 8601 Duration) | 0..1 | Aggregationsperiode (default: 0s = gesamter Zeitraum) |

### 3.4 Aggregation (Clause 4.5.19)

Erlaubte Werte fuer `aggrMethods`:

| Methode | Beschreibung |
|---------|--------------|
| `avg` | Durchschnitt |
| `min` | Minimum |
| `max` | Maximum |
| `sum` | Summe |
| `sumsq` | Quadratsumme |
| `stddev` | Standardabweichung |
| `distinctCount` | Anzahl unterschiedlicher Werte |

---

## 4. Endpunkte

### 4.1 POST /temporal/entities

**Operation:** Create or Update (Upsert) Temporal Evolution of an Entity (Clause 5.6.11, REST 6.18.3.1)

Erstellt eine neue temporale Entity oder aktualisiert eine bestehende durch Hinzufuegen neuer Attribut-Instanzen.

#### URL-Parameter

| Parameter | Typ | Kardinalitaet | Beschreibung |
|-----------|-----|---------------|--------------|
| `local` | Boolean | 0..1 | Local Query Parameter |

#### Request Body

| Datentyp | Kardinalitaet | Beschreibung |
|----------|---------------|--------------|
| `EntityTemporal` | 1 | JSON-LD Objekt mit der temporalen Repraesentation der Entity |

#### Responses

| Code | Body | Beschreibung |
|------|------|--------------|
| 201 Created | N/A | Entity erstellt. `Location`-Header mit URI der erstellten Entity |
| 204 No Content | N/A | Entity aktualisiert (existierte bereits) |
| 400 Bad Request | ProblemDetails | Fehlerhafter Request |
| 422 Unprocessable Entity | ProblemDetails | Operation nicht verfuegbar |

---

### 4.2 GET /temporal/entities

**Operation:** Query Temporal Evolution of Entities (Clause 5.7.4, REST 6.18.3.2)

Abfrage der temporalen Entwicklung mehrerer Entities mit umfangreichen Filter-, Geo-, und Temporal-Parametern.

#### URL-Parameter

| Parameter | Typ | Kardinalitaet | Beschreibung |
|-----------|-----|---------------|--------------|
| `id` | Comma-separated URIs | 0..1 | Entity-IDs zum Filtern |
| `idPattern` | Regex | 0..1 | ID-Muster zum Filtern |
| `type` | String | 0..1 (Pflicht wenn `attrs` fehlt, ausser bei local) | Entity-Typ(en) |
| `attrs` | Comma-separated Strings | 0..1 (Pflicht wenn `type` fehlt, ausser bei local) | Attribut-Selektion (deprecated Synonym fuer pick+q) |
| `pick` | Comma-separated Strings | 0..1 | Entity-Members die enthalten sein sollen |
| `omit` | Comma-separated Strings | 0..1 | Entity-Members die ausgeschlossen werden sollen |
| `q` | String | 0..1 | NGSI-LD Query (Clause 4.9) |
| `csf` | String | 0..1 | Context Source Filter |
| `geometry` | String | 0..1 (Pflicht wenn `georel` oder `coordinates` vorhanden) | Geometrie fuer Geo-Query (Clause 4.10) |
| `georel` | String | 0..1 (Pflicht wenn `geometry` oder `coordinates` vorhanden) | Geo-Relationship |
| `coordinates` | String | 0..1 (Pflicht wenn `georel` oder `geometry` vorhanden) | Koordinaten fuer Geo-Query |
| `geoproperty` | String | 0..1 | GeoProperty-Name (default: `location`) |
| `timerel` | String | **1** | Temporale Beziehung: `before`, `after`, `between` |
| `timeAt` | DateTime | **1** | Vergleichszeitpunkt |
| `endTimeAt` | DateTime | 0..1 (Pflicht bei `between`) | Endzeitpunkt |
| `timeproperty` | String | 0..1 | Temporal Property (default: `observedAt`) |
| `lastN` | Positive Integer | 0..1 | Letzte N Instanzen pro Attribut/Entity |
| `lang` | String | 0..1 | Bevorzugte Sprache fuer LanguageMaps |
| `aggrMethods` | Comma-separated Strings | 0..1 (Pflicht wenn `aggregatedValues` in options) | Aggregationsmethoden |
| `aggrPeriodDuration` | String | 0..1 | Aggregationsperiode (ISO 8601 Duration) |
| `scopeQ` | String | 0..1 | Scope Query (Clause 4.19) |
| `datasetId` | Comma-separated URIs | 0..1 | Dataset-IDs zum Filtern |
| `expandValues` | Comma-separated Strings | 0..1 | Attribut-Werte die als URIs expandiert werden sollen |
| `jsonKeys` | Comma-separated Strings | 0..1 | Attribut-Werte die nicht als JSON-LD expandiert werden sollen |
| `orderBy` | String | 0..1 | Sortierung (Entity-Member + asc/desc) |
| `collation` | String | 0..1 | ICU Collation fuer Sortierung |
| `splitEntities` | Boolean | 0..1 | Verteilte Entities beruecksichtigen |
| `entityMap` | Boolean | 0..1 | EntityMap in Response zurueckgeben |
| `entityMapLifetime` | String | 0..1 | Gewuenschte Lebensdauer der EntityMap (ISO 8601) |
| `limit` | Positive Integer | 0..1 | Maximale Anzahl Ergebnisse |
| `count` | Boolean | 0..1 | Gesamtanzahl in Response-Header |
| `options` | String | 0..1 | Optionen: `sysAttrs`, `aggregatedValues`, `temporalValues` |
| `format` | String | 0..1 | Ausgabeformat: `normalized`, `concise`, `simplified`, `aggregated` |
| `local` | Boolean | 0..1 | Nur lokale Daten |

#### Responses

| Code | Body | Beschreibung |
|------|------|--------------|
| 200 OK | EntityTemporal[] | Erfolgreiche Abfrage |
| 201 Created | EntityTemporal[] | EntityMap wurde (neu) erstellt. `NGSILD-EntityMap`-Header |
| 203 Non-Authoritative | EntityTemporal[] | Angepasste Response (Spec-Version-Konformitaet) |
| 400 Bad Request | ProblemDetails | Fehlerhafter Request |

**Response-Header:** `NGSILD-Results-Count`, `NGSILD-Warning`, `NGSILD-EntityMap`, `NGSILD-Tenant`

---

### 4.3 GET /temporal/entities/{entityId}

**Operation:** Retrieve Temporal Evolution of an Entity (Clause 5.7.3, REST 6.19.3.1)

Abfrage der temporalen Entwicklung einer einzelnen Entity.

#### Path-Parameter

| Parameter | Beschreibung |
|-----------|--------------|
| `entityId` | URI der Entity |

#### URL-Parameter

| Parameter | Typ | Kardinalitaet | Beschreibung |
|-----------|-----|---------------|--------------|
| `attrs` | Comma-separated Strings | 0..1 | Attribut-Selektion (deprecated Synonym fuer pick) |
| `pick` | Comma-separated Strings | 0..1 | Entity-Members die enthalten sein sollen |
| `omit` | Comma-separated Strings | 0..1 | Entity-Members die ausgeschlossen werden sollen |
| `timerel` | String | 0..1 (Pflicht wenn `timeAt` vorhanden) | Temporale Beziehung |
| `timeAt` | DateTime | 0..1 (Pflicht wenn `timerel` vorhanden) | Vergleichszeitpunkt |
| `endTimeAt` | DateTime | 0..1 (Pflicht bei `between`) | Endzeitpunkt |
| `timeproperty` | String | 0..1 | Temporal Property (default: `observedAt`) |
| `lastN` | Positive Integer | 0..1 | Letzte N Attribut-Instanzen |
| `lang` | String | 0..1 | Bevorzugte Sprache |
| `aggrMethods` | Comma-separated Strings | 0..1 (Pflicht wenn `aggregatedValues` in options) | Aggregationsmethoden |
| `aggrPeriodDuration` | String | 0..1 | Aggregationsperiode |
| `datasetId` | Comma-separated URIs | 0..1 | Dataset-IDs |
| `options` | String | 0..1 | Optionen: `sysAttrs`, `aggregatedValues`, `temporalValues` |
| `format` | String | 0..1 | Ausgabeformat |
| `local` | Boolean | 0..1 | Nur lokale Daten |

#### Responses

| Code | Body | Beschreibung |
|------|------|--------------|
| 200 OK | EntityTemporal | Temporale Repraesentation der Entity |
| 203 Non-Authoritative | EntityTemporal | Angepasste Response |
| 400 Bad Request | ProblemDetails | Fehlerhafter Request |
| 404 Not Found | ProblemDetails | Entity nicht gefunden |

---

### 4.4 DELETE /temporal/entities/{entityId}

**Operation:** Delete Temporal Evolution of an Entity (Clause 5.6.16, REST 6.19.3.2)

Loescht die gesamte temporale Repraesentation einer Entity.

#### Path-Parameter

| Parameter | Beschreibung |
|-----------|--------------|
| `entityId` | URI der Entity |

#### URL-Parameter

| Parameter | Typ | Kardinalitaet | Beschreibung |
|-----------|-----|---------------|--------------|
| `local` | Boolean | 0..1 | Local Query Parameter |

#### Responses

| Code | Body | Beschreibung |
|------|------|--------------|
| 204 No Content | N/A | Erfolgreich geloescht |
| 400 Bad Request | ProblemDetails | Fehlerhafter Request |
| 404 Not Found | ProblemDetails | Entity nicht gefunden |

---

### 4.5 POST /temporal/entities/{entityId}/attrs

**Operation:** Add Attributes to Temporal Evolution of an Entity (Clause 5.6.12, REST 6.20.3.1)

Fuegt Attribut-Instanzen zur temporalen Repraesentation einer Entity hinzu.

#### Path-Parameter

| Parameter | Beschreibung |
|-----------|--------------|
| `entityId` | URI der Entity |

#### URL-Parameter

| Parameter | Typ | Kardinalitaet | Beschreibung |
|-----------|-----|---------------|--------------|
| `local` | Boolean | 0..1 | Local Query Parameter |

#### Request Body

| Datentyp | Kardinalitaet | Beschreibung |
|----------|---------------|--------------|
| `EntityTemporalFragment` | 1 | Vollstaendige Repraesentation der hinzuzufuegenden Attribut-Instanzen |

#### Responses

| Code | Body | Beschreibung |
|------|------|--------------|
| 204 No Content | N/A | Alle Attribute erfolgreich hinzugefuegt |
| 400 Bad Request | ProblemDetails | Fehlerhafter Request |
| 404 Not Found | ProblemDetails | Entity nicht gefunden |

---

### 4.6 DELETE /temporal/entities/{entityId}/attrs/{attrId}

**Operation:** Delete Attribute from Temporal Evolution of an Entity (Clause 5.6.13, REST 6.21.3.1)

Loescht ein Attribut (alle Instanzen oder nach datasetId) aus der temporalen Repraesentation.

#### Path-Parameter

| Parameter | Beschreibung |
|-----------|--------------|
| `entityId` | URI der Entity |
| `attrId` | Attribut-Name (Property oder Relationship) |

#### URL-Parameter

| Parameter | Typ | Kardinalitaet | Beschreibung |
|-----------|-----|---------------|--------------|
| `datasetId` | URI | 0..1 | Spezifisches Dataset zum Loeschen |
| `deleteAll` | Boolean | 0..1 | Wenn `true`, alle Instanzen loeschen. Andernfalls nur die per `datasetId` spezifizierte (oder Default-Instanz). |
| `local` | Boolean | 0..1 | Local Query Parameter |

#### Responses

| Code | Body | Beschreibung |
|------|------|--------------|
| 204 No Content | N/A | Erfolgreich geloescht |
| 400 Bad Request | ProblemDetails | Fehlerhafter Request |
| 404 Not Found | ProblemDetails | Entity oder Attribut nicht gefunden |

---

### 4.7 PATCH /temporal/entities/{entityId}/attrs/{attrId}/{instanceId}

**Operation:** Modify Attribute Instance in Temporal Evolution of an Entity (Clause 5.6.14, REST 6.22.3.1)

Partielle Aktualisierung einer spezifischen Attribut-Instanz.

#### Path-Parameter

| Parameter | Beschreibung |
|-----------|--------------|
| `entityId` | URI der Entity |
| `attrId` | Attribut-Name |
| `instanceId` | URI der Attribut-Instanz |

#### URL-Parameter

| Parameter | Typ | Kardinalitaet | Beschreibung |
|-----------|-----|---------------|--------------|
| `local` | Boolean | 0..1 | Local Query Parameter |

#### Request Body

| Datentyp | Kardinalitaet | Beschreibung |
|----------|---------------|--------------|
| `EntityTemporalFragment` | 1 | Vollstaendige Repraesentation der Attribut-Instanz |

#### Responses

| Code | Body | Beschreibung |
|------|------|--------------|
| 204 No Content | N/A | Erfolgreich aktualisiert |
| 400 Bad Request | ProblemDetails | Fehlerhafter Request |
| 404 Not Found | ProblemDetails | Entity, Attribut oder Instanz nicht gefunden |

---

### 4.8 DELETE /temporal/entities/{entityId}/attrs/{attrId}/{instanceId}

**Operation:** Delete Attribute Instance from Temporal Evolution of an Entity (Clause 5.6.15, REST 6.22.3.2)

Loescht eine einzelne Attribut-Instanz.

#### Path-Parameter

| Parameter | Beschreibung |
|-----------|--------------|
| `entityId` | URI der Entity |
| `attrId` | Attribut-Name |
| `instanceId` | URI der Attribut-Instanz |

#### URL-Parameter

| Parameter | Typ | Kardinalitaet | Beschreibung |
|-----------|-----|---------------|--------------|
| `local` | Boolean | 0..1 | Local Query Parameter |

#### Responses

| Code | Body | Beschreibung |
|------|------|--------------|
| 204 No Content | N/A | Erfolgreich geloescht |
| 400 Bad Request | ProblemDetails | Fehlerhafter Request |
| 404 Not Found | ProblemDetails | Entity, Attribut oder Instanz nicht gefunden |

---

### 4.9 POST /temporal/entityOperations/query

**Operation:** Query Temporal Evolution of Entities via POST (Clause 5.7.4, REST 6.24.3.1)

Alternative zu GET /temporal/entities fuer komplexe Abfragen. Vermeidet URL-Laengenbeschraenkungen und URL-Encoding.

#### URL-Parameter

| Parameter | Typ | Kardinalitaet | Beschreibung |
|-----------|-----|---------------|--------------|
| `local` | Boolean | 0..1 | Local Query Parameter |

#### Request Body

| Datentyp | Kardinalitaet | Beschreibung |
|----------|---------------|--------------|
| `Query` (mit `TemporalQuery`) | 1 | JSON-LD Objekt mit Entity-Filter, Geo-Query und Temporal-Query |

Der Query-Body enthaelt alle Parameter die bei GET als URL-Parameter uebergeben werden, strukturiert als JSON-Objekt (inkl. `entities`, `attrs`, `q`, `geoQ`, `temporalQ`, `scopeQ`).

#### Responses

| Code | Body | Beschreibung |
|------|------|--------------|
| 200 OK | EntityTemporal[] | Erfolgreiche Abfrage |
| 400 Bad Request | ProblemDetails | Fehlerhafter Request |

---

## 5. Gemeinsame HTTP-Header

### Request-Header

| Header | Beschreibung |
|--------|--------------|
| `Link` | JSON-LD @context als Link-Header (Alternative zu @context im Body) |
| `NGSILD-Tenant` | Multi-Tenancy: Tenant-Identifikator |
| `Content-Type` | `application/json` oder `application/ld+json` |
| `Accept` | Gewuenschtes Response-Format |

### Response-Header

| Header | Beschreibung |
|--------|--------------|
| `NGSILD-Tenant` | Echo des Request-Tenant |
| `NGSILD-Results-Count` | Gesamtanzahl der Ergebnisse (bei `count=true`) |
| `NGSILD-Warning` | Warnungen (z.B. partielle Ergebnisse) |
| `NGSILD-EntityMap` | URI der EntityMap-Ressource (bei `entityMap=true`) |
| `Location` | URI der erstellten Ressource (bei 201 Created) |
| `Preference-Applied` | Angewandte Spec-Version (bei 203) |
