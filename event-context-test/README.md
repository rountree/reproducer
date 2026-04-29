# Flux Event Context Test

Minimal test plugin to verify `flux_shell_add_event_context()` API behavior.

## Purpose

While debugging the Spindle event watch callback bug, we discovered that 
`spindle_port` and `spindle_num_ports` don't appear in the `guest.exec.eventlog`
even though Spindle's `sp_init` calls `flux_shell_add_event_context()` to add them.

This minimal test isolates the question: **Does `flux_shell_add_event_context()` work?**

## What it does

1. Registers a shell.init handler
2. Calls `flux_shell_add_event_context(shell, "shell.init", 0, "{s:i s:i}", "test_value", 42, "test_rank", rank)`
3. Checks if `test_value=42` appears in the guest.exec.eventlog

## Expected behavior

The shell.init event in guest.exec.eventlog should look like:
```
1234567.890 shell.init service="..." leader-rank=0 size=4 test_value=42 test_rank=0
```

## Running locally

```bash
cd reproducer/event-context-test
make
flux start ./run_test.sh
```

## Running on GitHub

Push to repository and manually trigger the `event-context-test` workflow.

## Questions this answers

- Does the API work at all in our Flux version?
- If it doesn't work, what error do we get?
- If it works, why doesn't Spindle's usage work?
- Is there a timing issue (event emitted before context added)?
