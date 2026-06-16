#!/bin/bash
# Test script to manually start 4 nodes and check cluster formation

set -x  # Enable debug output

echo "=== Cleaning up any existing containers ==="
docker rm -f ispn-node1 ispn-node2 ispn-node3 ispn-node4 2>/dev/null || true
docker network rm hotrod-test-cluster 2>/dev/null || true

echo ""
echo "=== Starting 4-node cluster ==="
cd /home/rigazilla/git/cpp17-client
./scripts/start_cluster.sh 4 > /tmp/cluster-test.log 2>&1

EXIT_CODE=$?
echo ""
echo "=== Script exit code: $EXIT_CODE ==="
echo ""

if [ $EXIT_CODE -ne 0 ]; then
    echo "ERROR: Script failed! Output:"
    cat /tmp/cluster-test.log
    exit 1
fi

echo "=== Cluster started successfully! ==="
echo ""
echo "Container status:"
docker ps --filter "name=ispn-node" --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"

echo ""
echo "Checking cluster size from node 1..."
sleep 5
CLUSTER_SIZE=$(docker exec ispn-node1 bash -c "/opt/infinispan/bin/cli.sh -c http://localhost:11222 'ls /containers/default' 2>/dev/null | grep -c '^node'")
echo "Cluster size: $CLUSTER_SIZE nodes"

echo ""
echo "Environment variables exported:"
cat /tmp/cluster-env.sh

echo ""
echo "=== Test complete! ==="
echo "To stop: docker rm -f ispn-node1 ispn-node2 ispn-node3 ispn-node4"
