#!/bin/bash
# Debug script to see what happens when adding node 4

echo "=== Testing add_cluster_node.sh for node 4 ==="
echo ""

# Clean start
echo "1. Cleaning up..."
docker rm -f $(docker ps -aq --filter "name=ispn-node") 2>/dev/null || true

# Start 3-node cluster
echo ""
echo "2. Starting 3-node cluster..."
cd /home/rigazilla/git/cpp17-client
./scripts/start_cluster.sh 3 > /tmp/cluster-start.log 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to start cluster"
    cat /tmp/cluster-start.log
    exit 1
fi

# Get cluster ID
CLUSTER_ID=$(grep "ISPN_CLUSTER_ID=" /tmp/cluster-env.sh | cut -d= -f2)
echo "Cluster ID: $CLUSTER_ID"

# Check initial state
echo ""
echo "3. Initial cluster state (3 nodes):"
docker ps --filter "name=ispn-node" --format "table {{.Names}}\t{{.Status}}"

# Try to add node 4
echo ""
echo "4. Adding node 4..."
./scripts/add_cluster_node.sh "$CLUSTER_ID" 4 > /tmp/add-node4.log 2>&1
ADD_RESULT=$?

echo "Exit code: $ADD_RESULT"
echo ""
echo "Script output:"
cat /tmp/add-node4.log

# Check if container started
echo ""
echo "5. Container state after add_cluster_node.sh:"
docker ps -a --filter "name=ispn-node4" --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"

# Check logs
echo ""
echo "6. Node 4 logs (last 30 lines):"
docker logs ispn-node4 2>&1 | tail -30 || echo "Container not found"

# Check cluster membership
echo ""
echo "7. Checking cluster membership:"
docker exec ispn-node1 curl -s http://localhost:11222/rest/v2/cluster | grep -o '"name":"[^"]*"' || echo "Failed to query cluster"

echo ""
echo "=== Debug complete ==="
