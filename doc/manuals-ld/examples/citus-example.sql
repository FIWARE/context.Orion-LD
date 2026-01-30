CREATE TABLE IF NOT EXISTS public.attributes
(
    instanceid text COLLATE pg_catalog."default" NOT NULL,
    id text COLLATE pg_catalog."default" NOT NULL,
    opmode operationmode,
    entityid text COLLATE pg_catalog."default" NOT NULL,
    observedat timestamp without time zone DEFAULT '1980-01-01 00:00:00'::timestamp without time zone,
    subproperties boolean,
    unitcode text COLLATE pg_catalog."default",
    datasetid character varying COLLATE pg_catalog."default" NOT NULL,
    valuetype valuetype,
    text text COLLATE pg_catalog."default",
    "boolean" boolean,
    "number" double precision,
    datetime timestamp without time zone,
    compound jsonb,
    geopoint geography(PointZ,4326),
    geomultipoint geography(MultiPointZ,4326),
    geopolygon geography(PolygonZ,4326),
    geomultipolygon geography(MultiPolygonZ,4326),
    geolinestring geography(LineStringZ,4326),
    geomultilinestring geography(MultiLineStringZ,4326),
    ts timestamp without time zone NOT NULL,
    CONSTRAINT uk_attributes UNIQUE (instanceid, entityid, observedat)
) PARTITION BY RANGE (observedat);

-- Distributed by entityid in Citus Cluster

-- Create default partition
CREATE TABLE public.attributes_null PARTITION OF public.attributes
    DEFAULT
TABLESPACE pg_default;

ALTER TABLE IF EXISTS public.attributes
    OWNER to postgres;

CREATE INDEX IF NOT EXISTS attributes_observedat_idx
    ON public.attributes USING btree
    (observedat ASC NULLS LAST);

CREATE INDEX IF NOT EXISTS attributes_entityid_id_idx
    ON public.attributes USING btree
    (entityid COLLATE pg_catalog."default" ASC NULLS LAST,
     id       COLLATE pg_catalog."default" ASC NULLS LAST);

CREATE INDEX IF NOT EXISTS attributes_entityid_id_observedat_idx
    ON public.attributes USING btree
    (entityid   COLLATE pg_catalog."default" ASC NULLS LAST,
     id         COLLATE pg_catalog."default" ASC NULLS LAST,
     observedat ASC NULLS LAST);

CREATE INDEX IF NOT EXISTS attributes_entityid_idx
    ON public.attributes USING btree
    (entityid COLLATE pg_catalog."default" ASC NULLS LAST);
