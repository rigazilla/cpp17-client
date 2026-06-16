#!/bin/bash
# Wait until cluster reaches expected size
#
# Usage: ./wait_for_cluster_size.sh <cluster_id> <expected_size>
#   cluster_id: Cluster ID from start_cluster.sh
#   expected_size: Expected number of cluster members

set -e

CLUSTER_ID=$1
EXPECTED_SIZE=$2

if [ -z "$CLUSTER_ID" ] || [ -z "$EXPECTED_SIZE" ]; then
    echo "ERROR: cluster_id and expected_size required" >&2
    echo "Usage: $0 <cluster_id> <expected_size>" >&2
    exit 1
fi

FIRST_CONTAINER="ispn-node1"  # Docker Compose V2 uses service name directly

# Check if first node exists in this project
EXISTING=$(docker ps --filter "label=com.docker.compose.project=${CLUSTER_ID}" \
                     --filter "label=com.docker.compose.service=ispn-node1" \
                     --format '{{.Names}}' 2>/dev/null)

if [ -z "$EXISTING" ]; then
    echo "ERROR: First node not running in cluster $CLUSTER_ID" >&2
    exit 1
fi

echo "Waiting for cluster size $EXPECTED_SIZE..." >&2

# Wait up to 60 seconds for cluster to reach expected size (4-node clusters need more time)
timeout 60 bash -c "
while true; do
    MEMBERS=\$(docker exec $FIRST_CONTAINER curl -s --digest -u admin:password \
        'http://localhost:11222/rest/v2/cluster?action=distribution' 2>/dev/null | \
        grep -o 'node_name' | wc -l || echo 0)

    echo \"Current cluster size: \$MEMBERS (want $EXPECTED_SIZE)\" >&2

    if [ \"\$MEMBERS\" -eq \"$EXPECTED_SIZE\" ]; then
        echo \"Cluster reached size $EXPECTED_SIZE\" >&2
        exit 0
    fi

    sleep 2
done
" || {
    echo "ERROR: Timeout waiting for cluster size $EXPECTED_SIZE" >&2
    exit 1
}
