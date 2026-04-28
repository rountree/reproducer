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
echo "======================================"

# Simple test command - just sleep briefly
# The bug is in the event watch callback delivery, not in what the job does
TEST_CMD="sleep 1"

echo "Running: flux alloc --nodes=$NODES --setopt=shell.plugins=/usr/lib/flux/shell/flux-event-watch-test.so bash -c \"$TEST_CMD\""
flux alloc --nodes=$NODES --setopt=shell.plugins=/usr/lib/flux/shell/flux-event-watch-test.so bash -c "$TEST_CMD"

echo ""
echo "Test completed"
