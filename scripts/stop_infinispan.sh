#!/bin/bash
# Stop and remove Infinispan container

set -e

CONTAINER_ID="$1"

if [ -z "$CONTAINER_ID" ]; then
    echo "Usage: $0 <container_id>" >&2
    exit 1
fi

# Stop and remove container (hide progress, show errors)
docker rm -f "$CONTAINER_ID" >/dev/null || true
