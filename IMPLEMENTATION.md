# Flux Event Watch Reproducer - Implementation Summary

## Overview

This minimal reproducer demonstrates a bug in flux-core v0.82.0 where `flux_job_event_watch()` callbacks silently fail to fire on some nodes in a multi-node cluster, even though the event watch registers successfully and the events exist in the KVS.

## Key Design Decisions

### 1. Shell Plugin Architecture (Not Standalone Application)

**Decision**: Implement as a flux shell plugin loaded by flux-shell on each node.

**Rationale**: This matches how Spindle encountered the bug. Each flux-shell instance (one per node) independently registers its own event watch, which is the exact scenario where callbacks fail. A standalone application would watch from a single location and wouldn't reproduce the distributed failure pattern.

### 2. Language: C (Not Python)

**Initial consideration**: Python for simplicity
**Final decision**: C

**Rationale**: Flux shell plugins must be C shared libraries loaded via `flux_plugin_init()`. The Python flux bindings are for writing job submission scripts, not shell plugins that run inside flux-shell processes.

### 3. Test Loop Structure: flux alloc Inside Loop

**Decision**: Put `flux alloc` inside the test iteration loop, not outside.

**Rationale per user**: "My suspicion is that we get into the error state or not when 'flux alloc' runs." In the original Spindle testing, either the first test would hang or none would. This suggests the bug is triggered during resource allocation/job startup, not during steady-state operation. Each iteration needs a fresh `flux alloc` to have independent chances of triggering the bug.

### 4. Success Detection: Log Analysis (Not Exit Codes)

**Decision**: Each rank logs `[SUCCESS rank=N]` when callback delivers shell.init event. Test harness counts SUCCESS messages to detect failures.

**Rationale**: The bug is silent - no errors are reported, callbacks just don't fire. Exit codes won't help. We need explicit logging from each rank to prove callbacks fired.

## Component Details

### flux-event-watch-test.c

Minimal C shell plugin (~170 lines) that:

1. **Plugin init**: Registers handler for shell.init phase
2. **shell.init callback**: 
   - Gets shell rank and job ID
   - Registers event watch: `flux_job_event_watch(h, jobid, "guest.exec.eventlog", 0)`
   - Registers callback: `flux_future_then(f, -1.0, event_watch_callback, ctx)`
   - Logs `[REGISTERED rank=N]` to stderr
3. **Event callback**: 
   - Logs `[EVENT rank=N] Received event: ...`
   - When shell.init event seen, logs `[SUCCESS rank=N]`
   - Stops watching

**Key logging points**:
- QQQ tracing (matching Spindle's debug approach) to track execution
- REGISTERED message proves watch registration succeeded
- EVENT messages prove callback is firing
- SUCCESS message proves shell.init was received

**What it doesn't do** (compared to Spindle):
- No actual workload spawning
- No spindle backend processes
- No library dependency handling
- Just pure event watching and logging

### Docker Infrastructure

**Base image**: `fluxrm/flux-sched:noble-v0.48.0-amd64` (contains flux-core v0.82.0 with the bug)

**What's included**:
- Multi-node Docker Compose setup (default 32 nodes)
- Munge for authentication
- Flux broker running on node-1, worker nodes connecting via SSH
- Plugin installed to `/usr/lib/flux/shell/flux-event-watch-test.so`

**What's removed** (compared to Spindle containers):
- Spindle build dependencies
- Spindle source code
- runTests and test suite infrastructure
- LaunchMON dependencies

### GitHub Actions Workflow

**event-watch-test.yml** runs:

1. Build 32-node cluster
2. Verify munge works
3. Run test loop (up to 100 iterations):
   - Execute `flux alloc --nodes=32 bash -c "sleep 1"` with timeout
   - Plugin runs on all 32 nodes and logs to stderr
   - Parse stderr to count `[REGISTERED rank=N]` and `[SUCCESS rank=N]` messages
   - If `SUCCESS` count < node count, bug is reproduced
4. On bug detection:
   - Identify which ranks failed (no SUCCESS message)
   - Collect flux diagnostics from all nodes:
     - `flux jobs -a` (job list)
     - `flux job eventlog $JOBID` (main eventlog)
     - `flux job eventlog -p guest.exec.eventlog $JOBID` (guest exec eventlog)
     - `flux dmesg` (broker logs)
   - Upload all logs as GitHub Actions artifacts
   - Exit with error

**Key differences from hang-32.yml**:
- No spindle log collection
- No GDB backtraces (nothing to backtrace - processes aren't hung, callbacks just didn't fire)
- No core files
- Simpler success detection (just grep/count SUCCESS messages)
- Stops on first bug detection (don't need multiple samples)

## Expected Behavior

### Bug Present (flux-core v0.82.0)

Within ~10-20 iterations, should see:
```
Registered: 32, Successful: 25
*** BUG DETECTED: Only 25/32 ranks received callbacks ***
  Rank 5 FAILED to receive callback
  Rank 12 FAILED to receive callback
  ...
```

Examining the flux eventlogs should show shell.init event IS present for all ranks, but callbacks silently failed to fire on 3-7 ranks.

### Bug Fixed (future flux-core version)

All iterations show:
```
Registered: 32, Successful: 32
```

All ranks receive callbacks successfully.

## Testing Approach

### Initial Testing (32 nodes)

Start with 32 nodes because:
- That's what reliably reproduced the bug in Spindle testing
- ~9-22% failure rate at 32 nodes means we expect 3-7 failures per run
- High likelihood of reproducing in a few iterations

### Future Experiments (from user requirements)

Once reproducer is working:
1. Test if bug rate remains constant with different node counts
2. Try single `flux alloc` with loop inside (vs current: loop around `flux alloc`)
   - This tests whether bug is in allocation phase or is persistent

## What This Proves to Flux Developers

When this reproducer triggers the bug, it demonstrates:

1. ✅ Event watch registration succeeds (no errors from `flux_job_event_watch()`)
2. ✅ Callback registration succeeds (no errors from `flux_future_then()`)
3. ✅ Events ARE in the KVS eventlog (verified by `flux job eventlog -p guest.exec.eventlog`)
4. ❌ Callbacks do NOT fire on 9-22% of nodes (no SUCCESS logs from those ranks)
5. ❌ Pattern is non-deterministic (different nodes fail in different runs)

This isolates the bug to flux-core's event watch callback delivery mechanism, removing all complexity from Spindle.

## Files Created

```
reproducer/
├── README                              # User documentation
├── IMPLEMENTATION.md                   # This file
├── flux-event-watch-test.c            # Minimal shell plugin
├── Makefile                           # Builds the plugin
├── run_test.sh                        # Test script (runs flux alloc)
├── docker/
│   ├── Dockerfile                     # Based on flux-sched:noble-v0.48.0
│   ├── docker-compose.yml.template    # Template for N nodes
│   └── generate_compose.sh            # Generates docker-compose.yml
└── .github/
    └── workflows/
        └── event-watch-test.yml       # GitHub Actions workflow
```

## Next Steps

1. User needs to fix SSH permissions and push to GitHub
2. Run workflow via GitHub Actions workflow_dispatch
3. Download artifacts when bug reproduces
4. Analyze logs to confirm it's the same bug pattern
5. Share reproducer with Flux development team
6. (Future) Experiment with node counts and flux alloc placement
