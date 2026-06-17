#!/bin/bash
# Start Infinispan server for quickstart example

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTAINER_NAME="infinispan-quickstart"

echo "=== Starting Infinispan Server for Quickstart ==="

# Check if container already running
if docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    echo "Container ${CONTAINER_NAME} is already running"
    echo "Server available at: localhost:11222"
    echo "Console: http://localhost:11222/console (admin/password)"
    exit 0
fi

# Remove old container if exists
if docker ps -a --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    echo "Removing old container..."
    docker rm -f ${CONTAINER_NAME} >/dev/null
fi

# Start Infinispan container with custom config (no authentication)
echo "Starting Infinispan container..."
docker run -d \
    --name ${CONTAINER_NAME} \
    -v "${SCRIPT_DIR}:/user-config:ro" \
    -p 11222:11222 \
    infinispan/server:16.2 \
    -c /user-config/infinispan-cluster.xml \
    >/dev/null

echo "Waiting for server to start..."

# Wait for server to start (simple 10 second wait)
echo "Waiting for server to start..."
sleep 10
echo "Server should be ready!"

# Create quickstart cache using CLI
echo "Creating quickstart cache..."
docker exec ${CONTAINER_NAME} \
    bash -c "echo 'create cache --template=org.infinispan.DIST_SYNC quickstart-cache' | /opt/infinispan/bin/cli.sh -c http://localhost:11222" \
    >/dev/null 2>&1 || echo "(Cache may already exist)"

echo
echo "=== Infinispan Server Started Successfully ==="
echo "Server URL: http://localhost:11222"
echo "Console: http://localhost:11222/console"
echo "Cache: quickstart-cache"
echo
echo "To stop the server, run: docker stop ${CONTAINER_NAME}"
echo "To remove the server, run: docker rm ${CONTAINER_NAME}"
echo
