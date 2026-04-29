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

echo ""
echo "=== TEST 1: flux run (direct job) ==="
echo "Running: flux run -o userrc=test.rc $TEST_CMD"
flux run -o userrc=test.rc $TEST_CMD

echo ""
echo "Checking eventlog for flux run job..."
JOBID_RUN=$(flux job last)
echo "Job ID: $JOBID_RUN"

flux job eventlog -p guest.exec.eventlog "$JOBID_RUN" > /tmp/test_eventlog_run.txt
echo "=== flux run guest.exec.eventlog ==="
cat /tmp/test_eventlog_run.txt
echo ""

if grep -q "test_value" /tmp/test_eventlog_run.txt; then
    echo "✓ SUCCESS: test_value found in flux run eventlog!"
    grep "test_value" /tmp/test_eventlog_run.txt
else
    echo "✗ FAILURE: test_value NOT found in flux run eventlog"
fi

echo ""
echo "=== TEST 2: flux alloc (allocation + nested job) ==="
echo "Running: flux alloc -o userrc=test.rc bash -c \"$TEST_CMD\""
flux alloc -o userrc=test.rc bash -c "$TEST_CMD"

echo ""
echo "Checking eventlog for flux alloc job..."
JOBID_ALLOC=$(flux job last)
echo "Allocation Job ID: $JOBID_ALLOC"

flux job eventlog -p guest.exec.eventlog "$JOBID_ALLOC" > /tmp/test_eventlog_alloc.txt
echo "=== flux alloc guest.exec.eventlog ==="
cat /tmp/test_eventlog_alloc.txt
echo ""

if grep -q "test_value" /tmp/test_eventlog_alloc.txt; then
    echo "✓ SUCCESS: test_value found in flux alloc eventlog!"
    grep "test_value" /tmp/test_eventlog_alloc.txt
else
    echo "✗ FAILURE: test_value NOT found in flux alloc eventlog"
    echo "This would explain why Spindle's spindle_port doesn't appear!"
fi

echo ""
echo "=== SUMMARY ==="
echo "flux run:   $(grep -q 'test_value' /tmp/test_eventlog_run.txt && echo 'WORKS' || echo 'FAILS')"
echo "flux alloc: $(grep -q 'test_value' /tmp/test_eventlog_alloc.txt && echo 'WORKS' || echo 'FAILS')"
