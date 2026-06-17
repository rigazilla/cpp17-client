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

# Start Infinispan container
echo "Starting Infinispan container..."
docker run -d \
    --name ${CONTAINER_NAME} \
    -p 11222:11222 \
    -e USER=admin \
    -e PASS=password \
    quay.io/infinispan/server:15.0 \
    >/dev/null

echo "Waiting for server to start..."

# Wait for server to be ready (max 30 seconds)
TIMEOUT=30
ELAPSED=0
while [ $ELAPSED -lt $TIMEOUT ]; do
    if curl -s -f -u admin:password http://localhost:11222/rest/v3/server/ >/dev/null 2>&1; then
        echo "Server is ready!"
        break
    fi
    sleep 1
    ELAPSED=$((ELAPSED + 1))
    echo -n "."
done
echo

if [ $ELAPSED -ge $TIMEOUT ]; then
    echo "ERROR: Server failed to start within ${TIMEOUT} seconds"
    docker logs ${CONTAINER_NAME}
    exit 1
fi

# Create default cache (using distributed mode)
echo "Creating default cache..."
docker exec ${CONTAINER_NAME} /opt/infinispan/bin/cli.sh -c http://localhost:11222 \
    --username=admin --password=password \
    <<EOF >/dev/null 2>&1 || echo "Cache may already exist"
create cache --template=org.infinispan.DIST_SYNC ___defaultcache
EOF

echo
echo "=== Infinispan Server Started Successfully ==="
echo "Server URL: http://localhost:11222"
echo "Console: http://localhost:11222/console"
echo "Username: admin"
echo "Password: password"
echo "Cache: ___defaultcache"
echo
echo "To stop the server, run: docker stop ${CONTAINER_NAME}"
echo "To remove the server, run: docker rm ${CONTAINER_NAME}"
echo
