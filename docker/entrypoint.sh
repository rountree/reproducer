#!/bin/bash
#
# Simplified entrypoint for flux event watch reproducer
# No munge needed - just flux
#

set -e

echo "Starting flux on $(hostname)"
echo "Environment: workers=${workers}"

# Check if workers is set
if [ -z "${workers}" ]; then
    echo "ERROR: workers environment variable not set"
    exit 1
fi

echo "Starting flux with --test-size=${workers}"

# Use flux start in test mode - no authentication needed
exec flux start --test-size=${workers} sleep infinity
