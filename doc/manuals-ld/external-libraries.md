# External Libraries
The LD part of Orion-LD depends on the following external libraries:
* microhttpd
* libmongoc (MongoDB C driver) - requires **MongoDB 4.2+**
* libcurl
* libuuid
* kbase
* klog
* kalloc
* khash
* kjson
* librdkafka (optional, for Kafka consumer subsystem — only needed when using `-kafka` flag)

## MongoDB Version Requirement
Orion-LD uses libmongoc (the MongoDB C driver), which requires MongoDB wire protocol version 8 or higher.
This means **MongoDB 4.2 or higher** is required. Earlier versions (4.0, 3.6, etc.) are not supported.

The "heart" of the linked-data extension of orionld is the [kjson](https://gitlab.com/kzangeli/kjson) library.
kjson takes care of the parsing of incoming JSON and transforms the textual input as a tree of KjNode structs.

A KjNode tree is basically a linked list of attributes (id and type are some kind of attributes of the entity, right?)
that can have children, the attribute metadata.
In NGSI-LD the attribute metadata takes another name, namely Property-of-Property, or Property-of-Relationhsip, or ...

Trees based on the KjNode structure isn't just for the incoming payload, but also for:
* outgoing payload
* context cache
* intermediate storage format for the DB abstaction layer
* etc.
