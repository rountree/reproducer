#!/bin/bash
#
# Entrypoint for multi-container flux cluster
#

set -e

# Determine hostname and rank
HOSTNAME=$(hostname)
echo "Starting flux on ${HOSTNAME}"

# Main node (rank 0) or worker node?
if [ "${HOSTNAME}" = "node-1" ]; then
    echo "This is the main node (rank 0)"
    IS_MAIN=1
else
    echo "This is a worker node"
    IS_MAIN=0
fi

# Main node: generate curve certificate and R configuration
if [ $IS_MAIN -eq 1 ]; then
    echo "Generating flux configuration for ${workers} nodes"

    # Create directories
    sudo mkdir -p /etc/flux/system /etc/flux/config /etc/flux/imp/conf.d /run/flux /var/lib/flux
    sudo chown -R fluxuser:fluxuser /etc/flux /run/flux /var/lib/flux

    # Generate curve certificate
    flux keygen /etc/flux/system/curve.cert

    # Generate resource configuration
    flux R encode --hosts="node-[1-${workers}]" > /etc/flux/system/R

    # Update broker.toml with actual node count
    sed "s/NODE_COUNT/${workers}/g" /home/fluxuser/flux-config/broker.toml > /etc/flux/config/broker.toml

    # Copy IMP config
    cp /home/fluxuser/flux-config/imp.toml /etc/flux/imp/conf.d/imp.toml

    echo "Configuration complete, starting flux broker"
else
    echo "Worker node waiting for main node to initialize..."
    sleep 15
fi

# Start flux broker
BROKER_OPTS="-Srundir=/run/flux"
BROKER_OPTS="$BROKER_OPTS -Sstatedir=/var/lib/flux"
BROKER_OPTS="$BROKER_OPTS -Slocal-uri=local:///run/flux/local"
BROKER_OPTS="$BROKER_OPTS -Slog-stderr-level=7"
BROKER_OPTS="$BROKER_OPTS -Slog-stderr-mode=local"

# Ensure socket directory exists with correct permissions
sudo mkdir -p /run/flux
sudo chown fluxuser:fluxuser /run/flux

if [ $IS_MAIN -eq 1 ]; then
    # Main node
    flux start -o --config /etc/flux/config ${BROKER_OPTS} sleep infinity
else
    # Worker node
    flux start -o --config /etc/flux/config ${BROKER_OPTS} sleep infinity
fi
