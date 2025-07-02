# Functional Test Suite of Orion-LD
Orion-LD is a fork of Orion and the same test harness for functional tests is used, with quite a few modification but still the same
basics.

## Directory Structure
The functional tests of Orion-LD is a set of thousands of test cases, each case as an individual file under the directory:
```
test/functionalTest/cases
```
The tests of NGSIv1/v2 are structured as a directory per issue, with a number of test cases under those directories, e.g.:
```
test/functionalTest/cases/0117_convop_using_standard_ops/
```

while the tests for NGSI-LD use a flatter structure, using (right now) only 3 subdirectories (all three under '0000_ld`:
- test/functionalTest/cases/0000_ld/ngsild/
- test/functionalTest/cases/0000_ld/troe/
- test/functionalTest/cases/0000_ld/dds/

The test harness itself is a bash script, with a helper file defining functions used in the test harness:
- test/functionalTest/testHarness.sh
- test/functionalTest/harnessFunctions.sh

It is not a bad idea to create an alias for `testHarness.sh` as the path is a little long ...
```
alias ft=test/functionalTest/testHarness.sh
```

## Installation
See the corresponding installation guides of functional tests, e.g. for [Ubuntu 20.04](installation-guide-functional-tests-ubuntu20.04.1.md).
## Running the Functional Test suite
The test suite must be started from the repo root directory, as it assumes the test cases are found under `test/functionalTest/cases`
If `testHarness.sh` is started without options, the complete suite (all test cases) are executed:
```
test/functionalTest/testHarness.sh
```

To instead execute one single test case, just give the filename (without directories) as a parameter:
```
test/functionalTest/testHarness.sh ngsild_unsupported_content_type.test
```

To run all NGSI-LD test cases (dds, troe, and ngsild sub-directories):
```
test/functionalTest/testHarness.sh -ld
```

To run only one of the three NGSI-LD sub-directories:
```
test/functionalTest/testHarness.sh dds
test/functionalTest/testHarness.sh ngsild
test/functionalTest/testHarness.sh troe
```

There are a number of command line options and environment variables, please use the `-u` option to see it all:
```
test/functionalTest/testHarness.sh -u
mar 01 jul 2025 11:54:04 CEST
Usage: testHarness.sh [-u (usage)]
                      [-v (verbose)]
                      [-t (trace level for broker)]
                      [-kt (ktrace level for broker)]
                      [--loud (loud - see travis extra info)]
                      [-ld (only ngsild tests)]
                      [-troe (only ngsild TRoE (Temporal Representation of Entities) tests)]
                      [-dds (only DDS tests)]
                      [-eb (external broker)]
                      [-tk (on error, show the diff using tkdiff)]
                      [-meld (on error, show the diff using meld)]
                      [-meld (on error, show the diff using diff)]
                      [--filter <test filter>]
                      [--match <string for test to match>]
                      [--keep (don't remove output files)]
                      [--dryrun (don't execute any tests)]
                      [--dir <directory>]
                      [--fromIx <index of test where to start>]
                      [--toIx <index of test where to end (inclusive)>]
                      [--ixList <list of test indexes>]
                      [--skipList <list of indexes of test cases to be skipped>]
                      [--stopOnError (stop at first error encountered)]
                      [--no-duration (removes duration mark on successful tests)]
                      [--noCache (force broker to be started with the option --noCache)]
                      [--cache (force broker to be started without the option --noCache)]
                      [--noThreadpool (do not use a threadpool, unless specified by a test case. If not set, a thread pool of 200:20 is used by default in test cases which do not set notificationMode options)]
                      [ <directory or file> ]*
                      [--errors (show errors from last run)]

* Please note that if a directory is passed as parameter, its entire path must be given, not only the directory-name
* If a file is passed as parameter, its entire file-name must be given, including '.test'

Env Vars:
CB_TRACELEVELS:          the trace-level string, as used with -t for the broker
CB_KTRACELEVELS:         the ktrace-level string, as used with -kt for the broker
CB_MAX_TRIES:            the number of tries before giving up on a failing test case
CB_SKIP_LIST:            default value for option --skipList
CB_SKIP_FUNC_TESTS:      comma-separated list of names of func tests to skip
CB_NO_CACHE:             Start the broker without subscription cache (if set to 'ON')
CB_THREADPOOL:           Start the broker without thread pool (if set to 'OFF')
CB_DIFF_TOOL:            To view diff of failing tests with diff/tkdiff/meld/...
CB_EXTERNAL_BROKER:      The broker is started externally - not 'automatically' by the test harness (if set to 'ON')
CB_CONTEXT_SERVER_DELAY: Delay to wait context server to be ready
```

## Implementing a Functional Test Case
There is a guide on how to implement a new functional test case [here](../manuals/devel/cookbook.md#adding-a-functional-test-case),
and also help on [how to debug one](../manuals/devel/cookbook.md#debug-a-functional-test-case).
