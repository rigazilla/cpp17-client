#!/bin/bash
# Dynamically add a node to running cluster
#
# Usage: ./add_cluster_node.sh <cluster_id> <node_number>
#   cluster_id: Cluster ID from start_cluster.sh
#   node_number: Node number to add (1-4)

set -e

CLUSTER_ID=$1
NODE_NUM=$2

if [ -z "$CLUSTER_ID" ] || [ -z "$NODE_NUM" ]; then
    echo "ERROR: cluster_id and node_number required" >&2
    echo "Usage: $0 <cluster_id> <node_number>" >&2
    exit 1
fi

if [ "$NODE_NUM" -lt 1 ] || [ "$NODE_NUM" -gt 4 ]; then
    echo "ERROR: node_number must be between 1 and 4" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_FILE="$SCRIPT_DIR/../test-configs/docker-compose-cluster.yml"

if [ ! -f "$COMPOSE_FILE" ]; then
    echo "ERROR: Docker Compose file not found: $COMPOSE_FILE" >&2
    exit 1
fi

SERVICE="ispn-node${NODE_NUM}"
CONTAINER="${SERVICE}"  # Docker Compose V2 uses service name directly

# Check if node already exists in this project
EXISTING=$(docker ps -a --filter "label=com.docker.compose.project=${CLUSTER_ID}" \
                      --filter "label=com.docker.compose.service=${SERVICE}" \
                      --format '{{.Names}}' 2>/dev/null)

if [ -n "$EXISTING" ]; then
    echo "ERROR: Node $NODE_NUM already exists in cluster $CLUSTER_ID" >&2
    exit 1
fi

echo "Adding node $NODE_NUM to cluster $CLUSTER_ID" >&2

# Start the new node (--no-deps to avoid recreating existing nodes)
docker compose -f "$COMPOSE_FILE" -p "$CLUSTER_ID" up -d --no-deps "$SERVICE"

# Wait for node to be healthy
echo "Waiting for node $NODE_NUM to be healthy..." >&2
timeout 60 bash -c "
while ! docker inspect --format='{{.State.Health.Status}}' $CONTAINER 2>/dev/null | grep -q 'healthy'; do
    sleep 2
done
" || {
    echo "ERROR: Node $NODE_NUM failed to become healthy" >&2
    docker logs "$CONTAINER" >&2 2>&1
    exit 1
}

echo "Node $NODE_NUM healthy" >&2

# Get node info
HOST="localhost"
PORT=$(docker port "$CONTAINER" 11222 2>/dev/null | cut -d: -f2)

if [ -z "$PORT" ]; then
    echo "ERROR: Failed to get mapped port for node $NODE_NUM" >&2
    exit 1
fi

# Output node info for C++ tests
echo "export ISPN_NODE${NODE_NUM}_HOST=$HOST"
echo "export ISPN_NODE${NODE_NUM}_PORT=$PORT"
echo "export ISPN_NODE${NODE_NUM}_CONTAINER=$CONTAINER"

echo "Node $NODE_NUM added: $HOST:$PORT (container: $CONTAINER)" >&2
