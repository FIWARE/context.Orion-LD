# Running GA Functional Tests Locally

Reproduce the GitHub Actions functional test environment on your machine.

## Prerequisites

These services must be running on localhost (you probably already have them):
- MongoDB
- PostgreSQL (postgis)
- Mosquitto
- Context Server (port 7080)

## Quick Way (script)

From the repo root:

```bash
# Run a specific test
./test/ga-functest.sh test/functionalTest/cases/0000_ngsild/ws_patch_entity.test

# Run all tests
./test/ga-functest.sh

# Run a GA shard range
./test/ga-functest.sh --fromIx 0 --toIx 700

# Cleanup
./test/ga-functest.sh --cleanup
```

## Interactive Way (manual)

For debugging failures — gives you a shell inside the container.

### 1. Build the test image (from repo root)

```bash
docker build -f docker/Dockerfile-test -t orion-ld-test:local .
```

### 2. Start the container interactively

```bash
docker run --name ga-functest --network host \
  -v ~/git/orionld-1:/opt/orion \
  -e CB_FT_VERBOSE=ON \
  -e ORIONLD_CORE_CONTEXT_DIR=/opt/orion/ldcontexts \
  --entrypoint /bin/bash \
  -it orion-ld-test:local
```

### 3. Inside the container

```bash
git config --global --add safe.directory /opt/orion
rm -rf BUILD_RELEASE/CMakeCache.txt BUILD_RELEASE/CMakeFiles
export PKG_CONFIG_PATH=/usr/local/lib64/pkgconfig:/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
cd /opt/orion
make install
. scripts/testEnv.sh
CB_DIFF_TOOL="diff -u" test/functionalTest/testHarness.sh ws_patch_entity.test
```

### 4. After the test

You stay inside the container. Check logs, re-run tests, start the broker manually:

```bash
orionld -fg    # foreground, see startup errors
```

### 5. Re-entering later

```bash
docker start ga-functest
docker exec -it ga-functest bash
```

## Gotchas

- **CMakeCache.txt**: Host paths (`/home/kz/...`) don't match container paths (`/opt/orion`). Delete `BUILD_RELEASE/CMakeCache.txt` and `BUILD_RELEASE/CMakeFiles` on first build inside the container.
- **git safe.directory**: The volume mount changes ownership. Run `git config --global --add safe.directory /opt/orion`.
- **PKG_CONFIG_PATH**: Not set by default in interactive mode. Export it before `make install`.
