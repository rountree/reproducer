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

echo "Generating docker-compose.yml for ${NODE_COUNT} nodes..."

# Start with the template up to NODE_TEMPLATE_START
sed -n '1,/# NODE_TEMPLATE_START/p' "$TEMPLATE" | head -n -1 > "$OUTPUT"

# Replace NODECOUNT in node-1 service
sed -i "s/NODECOUNT/${NODE_COUNT}/g" "$OUTPUT"

# Generate additional nodes (2 through NODE_COUNT)
if [ "$NODE_COUNT" -gt 1 ]; then
    for i in $(seq 2 $NODE_COUNT); do
        echo "" >> "$OUTPUT"
        sed -n '/# NODE_TEMPLATE_START/,/# NODE_TEMPLATE_END/p' "$TEMPLATE" | \
            sed -e "s/node-N/node-${i}/g" -e "s/flux-var-N/flux-var-${i}/g" -e "s/-N:/-${i}:/g" | \
            grep -v "NODE_TEMPLATE" >> "$OUTPUT"
    done
fi

# Add networks section
echo "" >> "$OUTPUT"
echo "networks:" >> "$OUTPUT"
echo "  flux-net:" >> "$OUTPUT"
echo "    driver: bridge" >> "$OUTPUT"
echo "" >> "$OUTPUT"

# Add volumes section
echo "volumes:" >> "$OUTPUT"
for i in $(seq 1 $NODE_COUNT); do
    echo "  flux-var-${i}:" >> "$OUTPUT"
done
echo "  munge-1:" >> "$OUTPUT"

echo "Generated $OUTPUT with ${NODE_COUNT} nodes"
