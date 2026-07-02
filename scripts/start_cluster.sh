#!/bin/bash
# Start Infinispan cluster with N nodes for topology tests
#
# Usage: ./start_cluster.sh [num_nodes]
#   num_nodes: Number of nodes to start (default: 3, max: 4)
#
# Output: Exports environment variables for C++ tests
#   ISPN_CLUSTER_ID - Unique cluster identifier
#   ISPN_NUM_NODES - Number of nodes started
#   ISPN_NODE1_HOST, ISPN_NODE1_PORT, ISPN_NODE1_CONTAINER - Node 1 info
#   ISPN_NODE2_HOST, ISPN_NODE2_PORT, ISPN_NODE2_CONTAINER - Node 2 info
#   ... etc for each node

set -e

NUM_NODES=${1:-3}

if [ "$NUM_NODES" -lt 2 ] || [ "$NUM_NODES" -gt 4 ]; then
    echo "ERROR: num_nodes must be between 2 and 4" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_FILE="$SCRIPT_DIR/../test-configs/docker-compose-cluster.yml"

if [ ! -f "$COMPOSE_FILE" ]; then
    echo "ERROR: Docker Compose file not found: $COMPOSE_FILE" >&2
    exit 1
fi

# Generate unique cluster ID
CLUSTER_ID="hotrod-test-$$"

# Determine which nodes to start
SERVICES="ispn-node1"
for i in $(seq 2 $NUM_NODES); do
    SERVICES="$SERVICES ispn-node$i"
done

# Start cluster with docker compose (hide progress, show errors)
docker compose -f "$COMPOSE_FILE" -p "$CLUSTER_ID" up -d $SERVICES >/dev/null

# Wait for all nodes to be healthy
for i in $(seq 1 $NUM_NODES); do
    SERVICE="ispn-node${i}"
    CONTAINER="${SERVICE}"  # Docker Compose V2 uses service name directly

    # Wait up to 60 seconds for healthy
    timeout 60 bash -c "
    while ! docker inspect --format='{{.State.Health.Status}}' $CONTAINER 2>/dev/null | grep -q 'healthy'; do
        sleep 2
    done
    " || {
        echo "ERROR: Node $i failed to become healthy" >&2
        docker logs "$CONTAINER" >&2 2>&1
        docker compose -f "$COMPOSE_FILE" -p "$CLUSTER_ID" down >/dev/null
        exit 1
    }
done

# Wait for cluster formation
echo "Waiting for cluster formation..." >&2
FIRST_CONTAINER="ispn-node1"  # Docker Compose V2 uses service name directly
timeout 30 bash -c "
while true; do
    MEMBERS=\$(docker exec $FIRST_CONTAINER curl -s --digest -u admin:password \
        'http://localhost:11222/rest/v3/cluster/_distribution' 2>/dev/null | \
        grep -o 'node_name' | wc -l || echo 0)

    if [ \"\$MEMBERS\" -ge \"$NUM_NODES\" ]; then
        break
    fi
    sleep 2
done
" || {
    echo "ERROR: Cluster failed to form with $NUM_NODES members" >&2
    docker compose -f "$COMPOSE_FILE" -p "$CLUSTER_ID" down >/dev/null
    exit 1
}

# Export cluster info for C++ tests
echo "export ISPN_CLUSTER_ID=$CLUSTER_ID"
echo "export ISPN_NUM_NODES=$NUM_NODES"

# Export each node's info
for i in $(seq 1 $NUM_NODES); do
    SERVICE="ispn-node${i}"
    CONTAINER="${SERVICE}"  # Docker Compose V2 uses service name directly
    HOST="localhost"

    # Get mapped port for Hot Rod (11222 -> mapped)
    PORT=$(docker port "$CONTAINER" 11222 2>/dev/null | cut -d: -f2)

    if [ -z "$PORT" ]; then
        echo "ERROR: Failed to get mapped port for node $i" >&2
        docker compose -f "$COMPOSE_FILE" -p "$CLUSTER_ID" down >/dev/null
        exit 1
    fi

    echo "export ISPN_NODE${i}_HOST=$HOST"
    echo "export ISPN_NODE${i}_PORT=$PORT"
    echo "export ISPN_NODE${i}_CONTAINER=$CONTAINER"

    echo "Node $i: $HOST:$PORT (container: $CONTAINER)" >&2
done
