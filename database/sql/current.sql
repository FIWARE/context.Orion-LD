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

CREATE TABLE IF NOT EXISTS entities (
    instanceId TEXT NOT NULL,
     ts TIMESTAMP NOT NULL,
     opMode OperationMode,
     id TEXT NOT NULL,
     type TEXT NOT NULL,
     CONSTRAINT entities_pkey PRIMARY KEY (instanceId,ts));

CREATE TABLE IF NOT EXISTS attributes (
    instanceId TEXT NOT NULL,
    id TEXT NOT NULL,
    opMode OperationMode,
    entityId TEXT NOT NULL,
    observedAt TIMESTAMP,
    subProperties BOOL,
    unitCode TEXT,
    datasetId VARCHAR NOT NULL,
    valueType ValueType,
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
    CONSTRAINT attributes_pkey PRIMARY KEY (instanceId,datasetId,ts));

CREATE TABLE IF NOT EXISTS subAttributes (
    instanceId TEXT NOT NULL,
    id TEXT NOT NULL,
    entityId TEXT NOT NULL,
    attrInstanceId TEXT NOT NULL,
    attrDatasetId VARCHAR NOT NULL,
    observedAt TIMESTAMP,
    unitCode TEXT,
    valueType ValueType,
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
    CONSTRAINT subattributes_pkey PRIMARY KEY (instanceId,ts));

CREATE INDEX subattributes_attributeid_index ON subAttributes (attrInstanceId,attrDatasetId);
