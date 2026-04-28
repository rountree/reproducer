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

# Ensure flux directories exist
sudo mkdir -p /run/flux /var/lib/flux
sudo chown fluxuser:fluxuser /run/flux /var/lib/flux

echo "Starting flux with --test-size=${workers}"
echo "Flux version: $(flux version)"

# Use flux start in test mode - no authentication needed
# Don't use exec so we can see errors
flux start --test-size=${workers} sleep infinity
echo "ERROR: flux start exited with code $?"
exit 1
