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

# Broker configuration options
BROKER_OPTS="-Srundir=/run/flux"
BROKER_OPTS="$BROKER_OPTS -Sstatedir=/var/lib/flux"
BROKER_OPTS="$BROKER_OPTS -Slocal-uri=local:///run/flux/local"
BROKER_OPTS="$BROKER_OPTS -Slog-stderr-level=7"
BROKER_OPTS="$BROKER_OPTS -Slog-stderr-mode=local"

echo "Broker options: $BROKER_OPTS"

# Use flux start in test mode - no authentication needed
# Don't use exec so we can see errors
set -x
flux start --test-size=${workers} -o ${BROKER_OPTS} sleep infinity
RC=$?
set +x
echo "ERROR: flux start exited with code $RC"
exit 1
