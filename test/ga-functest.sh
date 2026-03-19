#!/bin/bash
# Copyright 2026 FIWARE Foundation e.V.
#
# This file is part of Orion-LD Context Broker.
#
# Orion-LD Context Broker is free software: you can redistribute it and/or
# modify it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# Orion-LD Context Broker is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
# General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
#
# For those usages not covered by this license please contact with
# orionld at fiware dot org
#

#
# Run functional tests the same way GitHub Actions does.
# Usage: ./test/ga-functest.sh [testHarness args...]
#
# Examples:
#   ./test/ga-functest.sh                                   # all functional tests
#   ./test/ga-functest.sh --fromIx 0 --toIx 700             # test range (like GA shard)
#   ./test/ga-functest.sh test/functionalTest/cases/0000_ngsild/ngsild_new_subscription.test
#
# After the test run, the container stays alive. To inspect:
#   docker exec -it ga-functest bash
#
# To clean up everything:
#   ./test/ga-functest.sh --cleanup
#
# Or, just run it manually:
#   docker run --name ga-functest --network host -v ~/git/orionld-1:/opt/orion -e CB_FT_VERBOSE=ON -e ORIONLD_CORE_CONTEXT_DIR=/opt/orion/ldcontexts --entrypoint /bin/bash -it orion-ld-test:local
#
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
IMAGE_NAME="orion-ld-test:local"
CONTAINER_NAME="ga-functest"

# --- Cleanup mode ---
if [ "$1" = "--cleanup" ]; then
  echo "Cleaning up..."
  docker rm -f $CONTAINER_NAME 2>/dev/null
  echo "Done."
  exit 0
fi

# Uses services already running on localhost (mongo, postgres, mosquitto, context-server)
# via --network host

# --- Build test image ---
echo "Building test image..."
docker build -f "$REPO_DIR/docker/Dockerfile-test" -t "$IMAGE_NAME" "$REPO_DIR"
if [ $? -ne 0 ]; then
  echo "Build failed!"
  exit 1
fi

# --- Remove previous test container if it exists ---
docker rm -f $CONTAINER_NAME 2>/dev/null

# --- Run tests ---
if [ $# -eq 0 ]; then
  echo "Running all functional tests (like GA)..."
  docker run --name $CONTAINER_NAME --network host \
    -v "$REPO_DIR":/opt/orion \
    -e CB_FT_VERBOSE=ON \
    -e ORIONLD_CORE_CONTEXT_DIR=/opt/orion/ldcontexts \
    "$IMAGE_NAME" -s functional -dqt
  TEST_EXIT=$?
else
  echo "Running: testHarness.sh $@"
  docker run --name $CONTAINER_NAME --network host \
    -v "$REPO_DIR":/opt/orion \
    -e CB_FT_VERBOSE=ON \
    -e ORIONLD_CORE_CONTEXT_DIR=/opt/orion/ldcontexts \
    --entrypoint /bin/bash \
    "$IMAGE_NAME" -c "
      cd /opt/orion
      make install_scripts 2>/dev/null
      make install 2>/dev/null
      . scripts/testEnv.sh

      rm -Rf /tmp/mongodb && mkdir -p /tmp/mongodb
      mongod --dbpath /tmp/mongodb --nojournal --quiet > /dev/null 2>&1 &
      sleep 3

      CB_DIFF_TOOL=\"diff -u\" /opt/orion/test/functionalTest/testHarness.sh $@
    "
  TEST_EXIT=$?
fi

echo ""
echo "Test exited with code $TEST_EXIT"
echo "Container '$CONTAINER_NAME' is still available. To inspect:"
echo "  docker start $CONTAINER_NAME"
echo "  docker exec -it $CONTAINER_NAME bash"
echo ""
echo "To clean up everything:"
echo "  $0 --cleanup"

exit $TEST_EXIT
