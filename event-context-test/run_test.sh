#!/bin/bash
#
# Simple test to verify flux_shell_add_event_context works
#

set -e

echo "======================================"
echo "Flux Event Context Test"
echo "Plugin: /usr/lib/flux/shell/test-plugin.so"
echo "RC file: test.rc"
echo "======================================"

TEST_CMD="sleep 1"

echo "Running: flux run -o userrc=test.rc $TEST_CMD"
flux run -o userrc=test.rc $TEST_CMD

echo ""
echo "Test completed"
echo ""
echo "Checking for test_value in eventlog..."
JOBID=$(flux job last)
echo "Job ID: $JOBID"

flux job eventlog -p guest.exec.eventlog "$JOBID" > /tmp/test_eventlog.txt
echo ""
echo "=== Guest exec eventlog ==="
cat /tmp/test_eventlog.txt
echo ""

if grep -q "test_value" /tmp/test_eventlog.txt; then
    echo "SUCCESS: test_value found in eventlog!"
    grep "test_value" /tmp/test_eventlog.txt
else
    echo "FAILURE: test_value NOT found in eventlog"
    echo "Expected to see test_value=42 in shell.init event"
    exit 1
fi
