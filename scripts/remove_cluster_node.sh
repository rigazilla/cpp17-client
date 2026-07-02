#!/bin/bash
# Remove (stop) a node from running cluster
#
# Usage: ./remove_cluster_node.sh <cluster_id> <node_number>
#   cluster_id: Cluster ID from start_cluster.sh
#   node_number: Node number to remove (1-4)

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

# Check if node exists in this project
EXISTING=$(docker ps --filter "label=com.docker.compose.project=${CLUSTER_ID}" \
                     --filter "label=com.docker.compose.service=${SERVICE}" \
                     --format '{{.Names}}' 2>/dev/null)

if [ -z "$EXISTING" ]; then
    echo "WARNING: Node $NODE_NUM not running in cluster $CLUSTER_ID" >&2
    exit 0
fi

# Stop and remove the node (hide progress, show errors)
docker compose -f "$COMPOSE_FILE" -p "$CLUSTER_ID" stop "$SERVICE" >/dev/null
docker compose -f "$COMPOSE_FILE" -p "$CLUSTER_ID" rm -f "$SERVICE" >/dev/null
