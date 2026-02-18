# Orion-LD and K-Libs Coding Style Guide

This document describes the coding conventions used in Orion-LD and its supporting k-libraries (kjson, kalloc, kbase, ktrace, khash). Use this guide when contributing code to maintain consistency.

## Table of Contents
1. [File Organization](#1-file-organization)
2. [Header Files](#2-header-files)
3. [Source Files](#3-source-files)
4. [Naming Conventions](#4-naming-conventions)
5. [Formatting](#5-formatting)
6. [Comments](#6-comments)
7. [Memory Management](#7-memory-management)
8. [Error Handling](#8-error-handling)
9. [Logging and Tracing](#9-logging-and-tracing)
10. [Common Patterns](#10-common-patterns)
11. [K-Libs Specifics](#11-k-libs-specifics)

---

## 1. File Organization

### Directory Structure
- Code organized by functionality under `src/lib/orionld/` and `src/app/orionld/`
- Subdirectories for features: `contextCache/`, `dbModel/`, `mhd/`, `q/`, `mongoc/`, etc.
- Each function in its own module (source file and its header file)
  - E.g., `X.cpp` contains the function `X()`)
  - Helper functions only used by X() are static in `X.c`.

### File Naming
- Descriptive camelCase: `orionldContextCacheInit.cpp`, `dbModelAttributePublishedAtLookup.cpp`
- Header and source files paired: `functionName.h` + `functionName.cpp`

---

## 2. Header Files

### Include Guards
The guard name is `REPONAME_FILENAME_H_` — the repo/library name as prefix, then the filename (camelCase uppercased, no underscores inserted), then `_H_` with trailing underscore.

For k-libs:
```c
#ifndef KJSON_KJPARSE_H_
#define KJSON_KJPARSE_H_

// content

#endif  // KJSON_KJPARSE_H_
```

For Orion-LD (includes directory path from repo root):
```cpp
#ifndef SRC_LIB_ORIONLD_CONTEXTCACHE_ORIONLDCONTEXTCACHE_H_
#define SRC_LIB_ORIONLD_CONTEXTCACHE_ORIONLDCONTEXTCACHE_H_

// content

#endif  // SRC_LIB_ORIONLD_CONTEXTCACHE_ORIONLDCONTEXTCACHE_H_
```

The repo prefix avoids collisions between identically named headers across libraries (e.g., `KBASE_VERSION_H_`, `KJSON_VERSION_H_`, `KHASH_VERSION_H_`).

### File Header (K-Libs)
Every `.c` and `.h` file in a k-lib must have the Apache 2.0 file header. Blank comment lines use `// ` (with trailing space).

For `.c` files, the header goes at the very top:
```c
//
// FILE            kaAlloc.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2019 Ken Zangelin
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with theLicense.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include <stdlib.h>                    // calloc
```

For `.h` files, the include guard comes first, then the header:
```c
#ifndef KALLOC_KAALLOC_H_
#define KALLOC_KAALLOC_H_

//
// FILE            kaAlloc.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2019 Ken Zangelin
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with theLicense.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include "kalloc/KAlloc.h"            // KAlloc



// -----------------------------------------------------------------------------
//
// kaAlloc -
//
extern char* kaAlloc(KAlloc* kaP, unsigned long long size);

#endif  // KALLOC_KAALLOC_H_
```

### Standard Structure (Orion-LD)
```cpp
/*
*
* Copyright 20XX FIWARE Foundation e.V.
*
* This file is part of Orion-LD Context Broker.
*
* [License text - GNU AGPL v3]
*
* Author: Ken Zangelin
*/
#ifndef INCLUDE_GUARD
#define INCLUDE_GUARD

#include <system_headers.h>                              // what's used

extern "C"
{
#include "kjson/KjNode.h"                                // KjNode
#include "ktrace/kTrace.h"                               // KT_*
}

#include "orionld/types/Type.h"                          // Type



// -----------------------------------------------------------------------------
//
// Declarations
//
extern Type  globalVar;
extern void  functionName(void);

#endif  // INCLUDE_GUARD
```

### Include Organization
1. System headers (`<stdio.h>`, `<string.h>`).
2. C library headers wrapped in `extern "C" { }` (kjson, kalloc, ktrace, etc.).
3. Third party headers.
4. Internal library headers, in order from most "basic" to less basic.
5. Own interface last (in .c files, not needed in C++ headers).

Each include has a trailing comment explaining what's used:
```cpp
#include "kjson/kjLookup.h"                              // kjLookup
#include "orionld/common/orionldState.h"                 // orionldState
```

---

## 3. Source Files

### Standard Structure
```cpp
/*
*
* Copyright 20XX FIWARE Foundation e.V.
*
* [License text]
*
* Author: Ken Zangelin
*/
#include <system.h>                                      // what's used

extern "C"
{
#include "kjson/KjNode.h"                                // KjNode
}

#include "orionld/common/orionldState.h"                 // orionldState
#include "orionld/module/thisFile.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// functionName -
//
ReturnType functionName(ParamType paramP)
{
  // implementation
}
```

### Function Documentation Block
```cpp
// -----------------------------------------------------------------------------
//
// functionName - brief description
//
// PARAMETERS
//   paramP    - description of parameter
//   outP      - output parameter description
//
// RETURN VALUE
//   Description of what is returned
//
// NOTE
//   Any additional notes about usage
//
```

---

## 4. Naming Conventions

### Functions
| Type | Pattern | Example |
|------|---------|---------|
| Public | `moduleName` + `Action` | `orionldContextCacheInit()` |
| Static/Internal | lowercase start | `attributeToSimplified()` |
| K-libs | prefix + camelCase | `kjObject()`, `kaAlloc()`, `ktInit()` |

### Variables
| Type | Pattern | Example |
|------|---------|---------|
| Pointers | suffix with `P` | `contextP`, `nodeP`, `entityP` |
| Arrays/Vectors | suffix with `V` | `attrV[]`, `connectionV` |
| Loop counters | `ix`, `i`, `j` | `for (int ix = 0; ...)` |
| Indices | suffix with `Ix` | `orionldContextCacheSlotIx` |
| Globals | descriptive, module-prefixed | `orionldContextCacheArray` |

### Types
| Type | Pattern | Example |
|------|---------|---------|
| Structs | CamelCase | `OrionldContext`, `PgConnectionPool` |
| Enums | CamelCase | `OrionldTraceLevels`, `KjValueType` |
| Enum values | Prefix + CamelCase | `KtResponse`, `KjString`, `SsPing` |
| Typedefs (k-libs) | K + name | `KBool`, `KjNode`, `KAlloc` |

### Macros
| Type | Pattern | Example |
|------|---------|---------|
| Constants | ALL_CAPS | `ORIONLD_CONTEXT_CACHE_SIZE` |
| Function-like | ALL_CAPS | `KJSON_ALLOC_CHECK()`, `KT_T()` |

---

## 5. Formatting

### Indentation
- 2 spaces per level (no tabs)
- Consistent throughout

### Pointer Declarations
The `*` belongs to the **type**, not the variable. The pointer is part of the type:
```c
// Correct — * pegged to the type
KjNode*  nodeP   = NULL;
char*    name    = "entity";
int*     countsP = NULL;

// Wrong — * pegged to the variable
KjNode *nodeP;
char *name;
```

### One Variable Per Line
Never declare multiple variables on the same line. This eliminates the `int *a, b` ambiguity entirely:
```c
// Correct
int   count = 0;
int   total = 0;
char* name  = NULL;

// Wrong — multiple variables on one line
int count, total;
int *a, b;  // b is not a pointer — this is why
```

### Parentheses Spacing
No space between a function name and `(`. Always a space between a keyword and `(`:
```c
// Functions — no space before (
kaAlloc(&kaP, size);
kjLookup(entityP, "id");
strcmp(a, b);

// Keywords — space before (
if (condition)
for (int ix = 0; ix < len; ix++)
while (running)
switch (type)
```

### Typecasts
No space inside the parentheses, one space after:
```c
int*   xP  = (int*) vP;
char*  buf = (char*) calloc(1, size);
float  f   = (float) intValue;
```

### Braces
```cpp
// Opening brace on same line for functions
void functionName(void)
{
  // body
}

// Control structures: brace on new line
if (condition)
{
  // body
}
else
{
  // body
}

// Single statement body: NEVER use braces
if (condition)
  singleStatement();

if (ptr == NULL)
  return NULL;

for (int ix = 0; ix < items; ix++)
  process(ix);

while (retries < 10)
  retry();

if (condition)
  doThis();
else
  doThat();
```

### Alignment
Align related declarations for readability:
```cpp
extern sem_t             orionldContextCacheSem;
extern OrionldContext*   orionldContextCacheArray[1000];
extern int               orionldContextCacheSlots;
extern int               orionldContextCacheSlotIx;
```

Align inline comments:
```cpp
KjNode*  idP    = kjLookup(entityP, "id");     // Entity ID
KjNode*  typeP  = kjLookup(entityP, "type");   // Entity Type
```

### Line Length
- Generally under 120 characters
- Break long lines at logical points

---

## 6. Comments

### C++ Style Only
Use `//` comments exclusively. Never use `/* */` block comments — they're ugly and don't nest. `//` comments have been standard C since C99 (1999).

### Inline Comment Spacing
Always leave **at least 2 spaces** before a trailing `//` comment:
```c
char* buf = kaAlloc(&kaP, size);  // Allocate from request pool
int   len = strlen(name);        // Cache length
```

Wrong:
```c
char* buf = kaAlloc(&kaP, size); // Too close
char* buf = kaAlloc(&kaP, size);// No space at all
```

### Section Separators
```cpp
// -----------------------------------------------------------------------------
//
// sectionName - description
//
```

### Three Blank Lines Before Separators
There must be **exactly 3 blank lines** before every `// -----` separator block. This applies everywhere: after the license header, after `#include` blocks, and between functions.

```c
#include "module/lastInclude.h"        // lastThing



// -----------------------------------------------------------------------------
//
// firstFunction -
//
int firstFunction(void)
{
  return 0;
}



// -----------------------------------------------------------------------------
//
// secondFunction -
//
void secondFunction(void)
{
  // ...
}
```

This visual spacing makes it easy to distinguish function boundaries when scrolling through source code.

### Multi-line Comments
```cpp
//
// Explanation spanning
// multiple lines
//
```

### Avoid
- Redundant comments that just repeat the code
- Commented-out code (delete it, use git history)

---

## 7. Memory Management (for short lived threads, typical in REST servers)

### Three Allocators
1. **kaAlloc** - Primary allocator (request-scoped, fast)
2. **kaStrdup** - String duplication with kaAlloc
3. **malloc** - When kaAlloc is unavailable or for long-lived allocations

### Patterns
```cpp
// Preferred: use kalloc
char* buf = kaAlloc(&orionldState.kalloc, size);

// String duplication
char* copy = kaStrdup(&orionldState.kalloc, original);

// Conditional allocation (prefer kalloc, fall back to malloc)
if (size < orionldState.kalloc.bytesLeft)
  buf = kaAlloc(&orionldState.kalloc, size);
else
{
  buf = (char*) malloc(size);
  orionldStateDelayedFreeEnqueue(buf);  // Ensure cleanup
}

// Buffer growth: use exponential, not linear
int newSize = oldSize * 2;  // Good
int newSize = oldSize + 512;  // Avoid - causes O(n²)
```

### Allocation Checks
```cpp
KjNode* nodeP = kjObject(orionldState.kjsonP, NULL);
KJSON_ALLOC_CHECK(nodeP);  // Returns/exits on NULL
```

---

## 8. Error Handling

### Return Patterns
- Functions return `NULL` or `-1` on failure
- Use error enums: `OrionldResponseErrorType`, `KjStatus`, `KaStatus`

### Error Reporting
```cpp
// Simple error with return
if (ptr == NULL)
  return NULL;

// Error with trace
if (ptr == NULL)
  KT_RE(NULL, "Failed to allocate buffer");

// Error with orionld error response (error info saved to be later returned in a HTTP response)
if (invalid)
{
  orionldError(OrionldBadRequestData, "Invalid parameter", paramName, 400);
  return false;
}

// Sequential checks (common pattern - to dave lines and ease readibility)
if (idP   == NULL) KT_RVE("No 'id' in entity");
if (typeP == NULL) KT_RVE("No 'type' in entity");
```

### NULL Checks
```cpp
// Explicit comparison preferred
if (ptr == NULL)
if (ptr != NULL)

// Conditional assignment with NULL check
KjNode* valueP = (objectP != NULL)? kjLookup(objectP, "value") : NULL;
```

---

## 9. Logging and Tracing

### KTrace Macros
| Macro | Purpose | Example |
|-------|---------|---------|
| `KT_T(level, ...)` | Trace (debug) | `KT_T(KtRequest, "Processing %s", url)` |
| `KT_V(...)` | Verbose | `KT_V("Detail info")` |
| `KT_W(...)` | Warning | `KT_W("Unexpected value")` |
| `KT_E(...)` | Error | `KT_E("Failed to connect")` |
| `KT_RE(ret, ...)` | Error + return | `KT_RE(NULL, "Allocation failed")` |
| `KT_RVE(...)` | Error + return void | `KT_RVE("Invalid state")` |
| `KT_X(code, ...)` | Fatal exit | `KT_X(1, "Out of memory")` |
| `KT_TREE(node, label, level)` | Dump JSON tree | `KT_TREE(entityP, "Entity", KtSR)` |

### Trace Levels
Defined in `orionld/common/traceLevels.h`:
- `KtRequest` (200) - Request processing
- `KtResponse` (201) - Response generation
- `KtMongoc` (500) - MongoDB operations
- `KtNotification` (1100) - Notifications
- `KtContext` (1700) - Context operations

### Performance Measurement
```cpp
PERFORMANCE(renderStart);
// ... code to measure ...
PERFORMANCE(renderEnd);
```

---

## 10. Common Patterns

### Extern "C" Wrapper
```cpp
extern "C"
{
#include "kjson/KjNode.h"                                // KjNode
#include "kalloc/kaAlloc.h"                              // kaAlloc
}
```

### Service Routine Tables
```cpp
static OrionLdRestService services[] =
{
  { HTTP_GET,    "/ngsi-ld/v1/entities",  getEntities  },
  { HTTP_POST,   "/ngsi-ld/v1/entities",  postEntities },
  { HTTP_NOVERB, "",                      NULL         }  // Terminator
};
```

### Switch Statements
```cpp
switch (nodeP->type)
{
case KjString:   handleString(nodeP);   break;
case KjInt:      handleInt(nodeP);      break;
case KjFloat:    handleFloat(nodeP);    break;
case KjObject:   handleObject(nodeP);   break;
case KjArray:    handleArray(nodeP);    break;
default:         handleDefault(nodeP);  break;
}
```

### Linked List Traversal (KjNode trees)
```cpp
for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
{
  // process attribute
}
```

### String Comparison
```cpp
// Use strcmp for string comparison
if (strcmp(attrP->name, "id") == 0)
  continue;

// Multiple comparisons
if (strcmp(name, "type")        == 0) continue;
if (strcmp(name, "id")          == 0) continue;
if (strcmp(name, "createdAt")   == 0) continue;
if (strcmp(name, "modifiedAt")  == 0) continue;
```

---

## 11. K-Libs Specifics

### Library Prefixes
| Library | Prefix | Example |
|---------|--------|---------|
| kjson | `kj` | `kjObject()`, `kjParse()`, `kjRender()` |
| kalloc | `ka` | `kaAlloc()`, `kaStrdup()`, `kaRealloc()` |
| kbase | `k` | `kTimeGet()`, `kStringSplit()` |
| ktrace | `kt` | `ktInit()`, `ktOut()` |
| khash | `khash` | `khashTableCreate()`, `khashItemLookup()` |

### K-Libs Are Pure C
- No C++ features in k-lib code
- Use `extern "C"` when including from C++

### K-Libs Error Handling
```c
// Return NULL on allocation failure
KjNode* nodeP = kjObject(kjP, name);
if (nodeP == NULL)
  KLOG_RE(NULL, "allocation failed");

// Status enums
typedef enum KjStatus { KjsOk, KjsBadParam, KjsAllocError, ... } KjStatus;
typedef enum KaStatus { KasOk, KasAllocError, ... } KaStatus;
```

### K-Libs Allocation Pattern
```c
// Prefer kalloc, fall back to malloc
char* buf = (kjP != NULL) ? kaAlloc(kjP->kallocP, size) : malloc(size);
```

---

## Quick Reference Card

```
NAMING:
  Functions:    orionldModuleAction(), kjAction()
  Pointers:     variableP
  Arrays:       variableV[]
  Indices:      variableIx
  Macros:       ALL_CAPS_WITH_UNDERSCORES
  Types:        CamelCase, KjNode, OrionldContext

FORMATTING:
  Indent:       2 spaces
  Braces:       New line for functions and multi-line if/for/while; no braces for single-statement bodies
  Pointers:     KjNode* nodeP (star pegged to type, one variable per line)
  Parens:       func() (no space), if () (space)
  Casts:        (char*) ptr (no space inside, space after)
  Comments:     // only (no /* */), at least 2 spaces before //
  Blank lines:  3 blank lines before every // ----- separator
  Line length:  ~120 chars max

MEMORY:
  Primary:      kaAlloc(&orionldState.kalloc, size)
  Strings:      kaStrdup(&orionldState.kalloc, str)
  Growth:       size * 2 (exponential, not linear)
  Check:        KJSON_ALLOC_CHECK(ptr)

ERRORS:
  Return:       KT_RE(NULL, "message") or KT_RVE("message")
  Report:       orionldError(type, title, detail, status)
  Log:          KT_E("error"), KT_W("warning"), KT_T(level, "trace")

INCLUDES:
  Order:        system -> extern "C" { k-libs } -> internal -> own interface
  Comment:      #include "file.h"  // WhatIsUsed
```
