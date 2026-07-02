#!/bin/bash
# Stop Infinispan cluster
#
# Usage: ./stop_cluster.sh <cluster_id>

set -e

CLUSTER_ID=$1

if [ -z "$CLUSTER_ID" ]; then
    echo "ERROR: cluster_id required" >&2
    echo "Usage: $0 <cluster_id>" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_FILE="$SCRIPT_DIR/../test-configs/docker-compose-cluster.yml"

if [ ! -f "$COMPOSE_FILE" ]; then
    echo "ERROR: Docker Compose file not found: $COMPOSE_FILE" >&2
    exit 1
fi

# Stop and remove all containers, networks, volumes (hide progress, show errors)
docker compose -f "$COMPOSE_FILE" -p "$CLUSTER_ID" down -v >/dev/null
