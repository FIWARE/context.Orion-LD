# Orion-LD Context Broker Roadmap

This product is a FIWARE Incubated Generic Enabler.
If you want to learn about the overall Roadmap of FIWARE, please have a look at the section "Roadmap" in the FIWARE Catalogue.

## Where the roadmap actually lives

The real, maintained roadmap is [Issue #280](https://github.com/FIWARE/context.Orion-LD/issues/280).
This file is only a summary and is updated far less often — if the two disagree, the issue wins.

## How Orion-LD is developed

It is worth being straightforward about this, so that expectations match reality:

* **DDS support is under active development**, as part of the European project ARISE. That is
  where most of the planned effort currently goes — see [DDS Integration](dds/orion-ld-dds.md).
* **Incoming issues are tackled.** Bug reports and questions from users drive most of everything
  else that happens. If something is broken and you report it, it gets looked at.
* **Occasional improvements happen simply because the maintainer felt like doing them.** They are
  real, but they are not planned, and nothing should be read into their timing.
* **Beyond that, Orion-LD is not receiving broad new feature development.** The main development
  focus has moved to a new NGSI-LD broker, written from scratch at Seamware (not yet publicly
  named).

Orion-LD remains maintained, in production use, and open to issues — it is simply not where large
new features are being built.

## Known planned work

* **haaux** — a second channel for cache synchronisation between broker instances (`-ha <ip:port>`),
  for deployments where MongoDB change streams are not an option. The MongoDB channel (`-ha mongo`)
  is done - see [High Availability](manuals-ld/high-availability.md).
* **An NGSI-LD-only Orion-LD**, dropping the inherited NGSIv2 layers.

For older items previously listed here (distributed subscriptions, GEOS-based geolocation for the
subscription cache), and for everything medium/long term, refer to
[Issue #280](https://github.com/FIWARE/context.Orion-LD/issues/280).
