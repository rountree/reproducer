#!/bin/bash
#
# Generate docker-compose.yml for N nodes from template
#

set -e

if [ $# -ne 1 ]; then
    echo "Usage: $0 <node_count>"
    exit 1
fi

NODE_COUNT=$1

if [ "$NODE_COUNT" -lt 1 ]; then
    echo "Error: node_count must be >= 1"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEMPLATE="${SCRIPT_DIR}/docker-compose.yml.template"
OUTPUT="${SCRIPT_DIR}/docker-compose.yml"

echo "Generating docker-compose.yml for ${NODE_COUNT} simulated nodes..."

# Copy template and replace NODECOUNT
cp "$TEMPLATE" "$OUTPUT"
sed -i "s/NODECOUNT/${NODE_COUNT}/g" "$OUTPUT"

echo "Generated $OUTPUT (single container, ${NODE_COUNT} simulated nodes via --test-size)"
