#!/bin/bash
# Cleanup all test containers
# Use this if tests fail to start due to "container name already in use"

echo "Cleaning up all ispn-node containers..."

# Remove all containers (running or stopped) with ispn-node prefix
CONTAINERS=$(docker ps -aq --filter "name=ispn-node" 2>/dev/null)

if [ -z "$CONTAINERS" ]; then
    echo "No ispn-node containers found"
else
    echo "Removing containers: $CONTAINERS"
    docker rm -f $CONTAINERS
    echo "✓ Cleanup complete"
fi

# Also clean up any test networks
NETWORKS=$(docker network ls --filter "name=hotrod-test" -q 2>/dev/null)
if [ -n "$NETWORKS" ]; then
    echo "Cleaning up test networks..."
    docker network rm $NETWORKS 2>/dev/null || true
fi

echo "✓ Ready to run tests"
