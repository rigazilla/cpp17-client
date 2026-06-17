#!/bin/bash
# Stop Infinispan server

set -e

CONTAINER_NAME="infinispan-quickstart"

echo "=== Stopping Infinispan Server ==="

if docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    echo "Stopping container ${CONTAINER_NAME}..."
    docker stop ${CONTAINER_NAME} >/dev/null
    echo "Container stopped"

    echo "Removing container ${CONTAINER_NAME}..."
    docker rm ${CONTAINER_NAME} >/dev/null
    echo "Container removed"

    echo "=== Server Stopped Successfully ==="
else
    echo "Container ${CONTAINER_NAME} is not running"
fi
