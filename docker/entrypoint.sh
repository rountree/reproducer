#!/bin/bash
#
# Simplified entrypoint for flux event watch reproducer
# No munge needed - just flux
#

set -e

echo "Starting flux on $(hostname)"

# Use flux start in test mode - no authentication needed
exec flux start --test-size=${workers} sleep infinity
