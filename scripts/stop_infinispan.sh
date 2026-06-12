#!/bin/bash
# Stop and remove Infinispan container

set -e

CONTAINER_ID="$1"

if [ -z "$CONTAINER_ID" ]; then
    echo "Usage: $0 <container_id>" >&2
    exit 1
fi

echo "Stopping Infinispan container: $CONTAINER_ID" >&2
docker rm -f "$CONTAINER_ID" 2>/dev/null || true
echo "Container stopped" >&2
