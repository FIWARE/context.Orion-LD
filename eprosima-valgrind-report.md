# Valgrind Report: Memory Issues in eProsima Fast DDS / DDS Enabler Libraries

**Date:** 2026-03-12
**Reporter:** FIWARE Orion-LD Context Broker team
**Application:** Orion-LD Context Broker (NGSI-LD)

## Environment

| Component | Version |
|-----------|---------|
| OS | Ubuntu 22.04.5 LTS (x86_64) |
| Kernel | 6.8.0-101-generic |
| Valgrind | 3.18.1 |
| Fast DDS | 3.3.0 (`libfastdds.so.3.3.0`) |
| Fast CDR | 2.3.0 (`libfastcdr.so.2.3.0`) |
| DDS Enabler | 1.0.0 (`libddsenabler.so.1.0.0`) |
| DDS Enabler Participants | 1.0.0 (`libddsenabler_participants.so.1.0.0`) |
| yaml-cpp | 0.7.0 (`libyaml-cpp.so.0.7.0`) |

## How We Test

Orion-LD runs its functional test suite under Valgrind (memcheck) with `--leak-check=full --show-leak-kinds=definite,indirect --track-origins=yes --trace-children=yes`. The broker uses the DDS Enabler library to publish and subscribe to DDS topics. The test suite exercises entity creation, attribute updates, and DDS publish/subscribe round-trips.

Valgrind suppression files are used for known issues in other libraries (libcurl/gnutls, mongoc, postgres). The issues below are **not** suppressed and originate entirely within eProsima libraries.

---

## Issue 1: Uninitialised Stack Variable in `Handler::get_type_identifier`

**Type:** Conditional jump or move depends on uninitialised value(s)
**Severity:** Medium — may cause incorrect behaviour; triggers 3 distinct error contexts per publish operation
**Reproducibility:** Every DDS publish operation

### Description

When `EnablerParticipant::publish()` is called, the code path through `get_type_identifier` → `register_type_nts_` → `deserialize_dynamic_types` uses an uninitialised stack variable. This manifests as three separate valgrind errors per publish:

### Error 1 of 3: `SerializedPayload_t::reserve`

```
Conditional jump or move depends on uninitialised value(s)
   at 0x4FA36A7: eprosima::fastdds::rtps::SerializedPayload_t::reserve(unsigned int) (libfastdds.so.3.3.0)
   by 0x64BE565: eprosima::ddsenabler::participants::serialization::deserialize_dynamic_types(...) (libddsenabler_participants.so.1.0.0)
   by 0x64A9D60: eprosima::ddsenabler::participants::Handler::register_type_nts_(...) (libddsenabler_participants.so.1.0.0)
   by 0x64AEBFF: eprosima::ddsenabler::participants::Handler::get_type_identifier(...) (libddsenabler_participants.so.1.0.0)
   by 0x64927D5: eprosima::ddsenabler::participants::EnablerParticipant::fill_topic_struct_nts_(...) (libddsenabler_participants.so.1.0.0)
   by 0x6492A6E: eprosima::ddsenabler::participants::EnablerParticipant::query_topic_nts_(...) (libddsenabler_participants.so.1.0.0)
   by 0x6497DBD: eprosima::ddsenabler::participants::EnablerParticipant::publish(...) (libddsenabler_participants.so.1.0.0)
 Uninitialised value was created by a stack allocation
   at 0x64AE97E: eprosima::ddsenabler::participants::Handler::get_type_identifier(...) (libddsenabler_participants.so.1.0.0)
```

### Error 2 of 3: `memmove` in `deserialize_dynamic_types`

```
Conditional jump or move depends on uninitialised value(s)
   at 0x485288A: memmove (vgpreload_memcheck-amd64-linux.so)
   by 0x64BE578: eprosima::ddsenabler::participants::serialization::deserialize_dynamic_types(...) (libddsenabler_participants.so.1.0.0)
   by 0x64A9D60: eprosima::ddsenabler::participants::Handler::register_type_nts_(...) (libddsenabler_participants.so.1.0.0)
   by 0x64AEBFF: eprosima::ddsenabler::participants::Handler::get_type_identifier(...) (libddsenabler_participants.so.1.0.0)
   ...
 Uninitialised value was created by a stack allocation
   at 0x64AE97E: eprosima::ddsenabler::participants::Handler::get_type_identifier(...) (libddsenabler_participants.so.1.0.0)
```

### Error 3 of 3: `Cdr::deserialize` in Fast CDR

```
Conditional jump or move depends on uninitialised value(s)
   at 0x6389522: eprosima::fastcdr::Cdr::deserialize(char&) (libfastcdr.so.2.3.0)
   by 0x638A090: eprosima::fastcdr::Cdr::read_encapsulation() (libfastcdr.so.2.3.0)
   by 0x64F8282: eprosima::ddsenabler::participants::DynamicTypesCollectionPubSubType::deserialize(...) (libddsenabler_participants.so.1.0.0)
   by 0x4C80B11: eprosima::fastdds::dds::TypeSupport::deserialize(...) (libfastdds.so.3.3.0)
   by 0x64BE586: eprosima::ddsenabler::participants::serialization::deserialize_dynamic_types(...) (libddsenabler_participants.so.1.0.0)
   by 0x64A9D60: eprosima::ddsenabler::participants::Handler::register_type_nts_(...) (libddsenabler_participants.so.1.0.0)
   by 0x64AEBFF: eprosima::ddsenabler::participants::Handler::get_type_identifier(...) (libddsenabler_participants.so.1.0.0)
   ...
 Uninitialised value was created by a stack allocation
   at 0x64AE97E: eprosima::ddsenabler::participants::Handler::get_type_identifier(...) (libddsenabler_participants.so.1.0.0)
```

### Root Cause

All three errors trace back to the same uninitialised stack variable created at `Handler::get_type_identifier` (offset `0x64AE97E`). This variable is passed down through `register_type_nts_` → `deserialize_dynamic_types`, where it is used as a buffer length or payload size without being initialised first.

### Suggested Fix

Initialise the stack variable in `Handler::get_type_identifier()` before passing it to `register_type_nts_()`.

---

## Issue 2: Memory Leak in yaml-cpp via `Writer::write_topic`

**Type:** Definite memory leak
**Severity:** Low — one-time static allocation (1,040 bytes), does not grow
**Reproducibility:** First DDS topic write operation

### Description

When a DDS topic is first written, the YAML serialization path in `libddsenabler_participants.so` triggers a one-time allocation inside `libyaml-cpp` that is registered via `__cxa_atexit` but reported as leaked by Valgrind.

```
1,040 bytes in 1 blocks are definitely lost in loss record 364 of 386
   at 0x484DA83: calloc (vgpreload_memcheck-amd64-linux.so)
   by 0x6199791: __new_exitfn (cxa_atexit.c:114)
   by 0x619990A: __internal_atexit (cxa_atexit.c:44)
   by 0x619990A: __cxa_atexit (cxa_atexit.c:70)
   by 0x6C0C845: ??? (libyaml-cpp.so.0.7.0)
   by 0x6C0CCB2: YAML::Emitter::Write(std::string const&) (libyaml-cpp.so.0.7.0)
   by 0x6C10DE4: ??? (libyaml-cpp.so.0.7.0)
   by 0x6C10FDB: ??? (libyaml-cpp.so.0.7.0)
   by 0x6C1327C: ??? (libyaml-cpp.so.0.7.0)
   by 0x6C090E7: YAML::operator<<(YAML::Emitter&, YAML::Node const&) (libyaml-cpp.so.0.7.0)
   by 0x6C093C8: YAML::Dump(YAML::Node const&) (libyaml-cpp.so.0.7.0)
   by 0x64BC387: eprosima::ddsenabler::participants::serialization::serialize_qos(...) (libddsenabler_participants.so.1.0.0)
   by 0x64C47E6: eprosima::ddsenabler::participants::Writer::write_topic(...) (libddsenabler_participants.so.1.0.0)
```

### Root Cause

`YAML::Emitter::Write` registers a static cleanup handler via `__cxa_atexit`. The 1,040 bytes allocated by `calloc` inside `__new_exitfn` are for the atexit handler registration itself. Since the process under Valgrind terminates before the atexit handlers can free this memory, it is reported as a definite leak. This may be a yaml-cpp issue rather than an eProsima issue, but the call originates from `serialize_qos` in `libddsenabler_participants.so`.

### Suggested Fix

This may be unavoidable given yaml-cpp's static initialisation pattern. If eProsima controls the YAML serialisation lifecycle, explicitly calling the YAML cleanup before process exit could resolve this. Alternatively, a Valgrind suppression could be added, but we prefer to report it.

---

## Issue 3: NULL Pointer Dereference in `DDSEnabler::publish` and `DDSEnabler::send_action_goal`

**Type:** Invalid read of size 8 (SIGSEGV)
**Severity:** High — causes process crash
**Reproducibility:** Intermittent under load; consistent under Valgrind (slower thread scheduling)

### Description

Both `DDSEnabler::publish()` and `DDSEnabler::send_action_goal()` dereference a NULL or invalid pointer at offset `0x278`, causing a SIGSEGV. This appears to be a race condition in DDS participant initialization — under Valgrind's serialised thread execution, the DDS enabler object may not be fully initialised when a publish or action is attempted.

### Crash in `DDSEnabler::publish`

```
Invalid read of size 8
   at 0x54FCAC4: eprosima::ddsenabler::DDSEnabler::publish(...) (libddsenabler.so.1.0.0)
   by 0x1F5DE6: ddsPublishAttribute(...) (ddsPublishAttribute.cpp:281)
   by 0x1F6C48: ddsPublishAttributes(...) (ddsPublishAttributes.cpp:77)
   by 0x1D2F93: orionldPatchEntity2() (orionldPatchEntity2.cpp:785)
   ...
 Address 0x278 is not stack'd, malloc'd or (recently) free'd

 Process terminating with default action of signal 11 (SIGSEGV)
  Access not within mapped region at address 0x278
   at 0x54FCAC4: eprosima::ddsenabler::DDSEnabler::publish(...) (libddsenabler.so.1.0.0)
```

### Crash in `DDSEnabler::send_action_goal`

```
Invalid read of size 8
   at 0x54FCB34: eprosima::ddsenabler::DDSEnabler::send_action_goal(...) (libddsenabler.so.1.0.0)
   by 0x1FC454: ddsActionGoalSend(...) (ddsAction.cpp:65)
   by 0x1F5BA9: ddsPublishAttribute(...) (ddsPublishAttribute.cpp:205)
   by 0x1F6CA6: ddsPublishAttributes(...) (ddsPublishAttributes.cpp:77)
   by 0x1D2FF1: orionldPatchEntity2() (orionldPatchEntity2.cpp:785)
   ...
 Address 0x278 is not stack'd, malloc'd or (recently) free'd
```

### Root Cause

Address `0x278` (decimal 632) is a small offset from NULL, indicating that a member variable is accessed on an object whose pointer is NULL or uninitialised. Both `publish()` and `send_action_goal()` appear to access the same internal object (likely a DDS participant or writer) at offset 632 from the base pointer.

This suggests a race condition: the DDS enabler's internal participant/writer is not yet fully constructed when `publish()` or `send_action_goal()` is called. Under normal execution, the thread scheduling may hide this, but under Valgrind's deterministic scheduling, the window is exposed.

### Suggested Fix

Add a NULL check or readiness guard in `DDSEnabler::publish()` and `DDSEnabler::send_action_goal()` before dereferencing internal pointers. Alternatively, ensure that `create_dds_enabler()` blocks until the participant is fully initialised.

---

## Notes

- The Orion-LD broker code itself has been verified clean of memory issues in these test paths (our own leaks were found and fixed during this analysis).
- The `possibly lost` and `still reachable` categories are not included in this report as they are typically benign (thread stacks, static globals).
- We are happy to provide full Valgrind output files, test cases, or assist with reproduction if needed.
