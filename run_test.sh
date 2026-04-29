#!/bin/bash
#
# Test script that runs flux alloc in a loop to trigger the event watch bug
#

set -e

# Get number of nodes from environment or default to 32
NODES=${NODES:-32}

echo "======================================"
echo "Flux Event Watch Test"
echo "Nodes: $NODES"
echo "Flux shell plugin: /usr/lib/flux/shell/flux-event-watch-test.so"
echo "RC file: event-watch-test.rc"
echo "======================================"

# TIMING TEST: Application must outlive all rank sleeps + event posting
# Rank 31 sleeps 15.5s before watching, rank 0 sleeps 17s before posting
# So we need at least 20 seconds to ensure the application is still running
TEST_CMD="sleep 20"

echo "Running: flux alloc --nodes=$NODES -o userrc=event-watch-test.rc -o spindle.level=high bash -c \"$TEST_CMD\""
flux alloc --nodes=$NODES -o userrc=event-watch-test.rc -o spindle.level=high bash -c "$TEST_CMD"

echo ""
echo "Test completed"
