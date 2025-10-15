#!/bin/bash
# Comprehensive test script for TSAM protocol implementation

echo "=== TSAM Protocol Test Suite ==="
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Kill any existing server
pkill -9 tsamgroup29 2>/dev/null
sleep 1

# Start server in background
echo "Starting server on port 4029..."
./tsamgroup29 4029 > /tmp/tsam_server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Test 1: Client sends and retrieves message
echo -e "${GREEN}Test 1: Client SENDMSG and GETMSG${NC}"
./tsamclient 127.0.0.1 4029 << 'EOF'
SENDMSG,A5_29,test message one
SENDMSG,A5_29,test message two
GETMSG
GETMSG
GETMSG
QUIT
EOF
echo ""

# Test 2: Peer sends message, client retrieves
echo -e "${GREEN}Test 2: Peer server sends message, client retrieves${NC}"
./test_peer 127.0.0.1 4029
sleep 1
echo -e "GETMSG\nQUIT" | ./tsamclient 127.0.0.1 4029
echo ""

# Test 3: Client sends to different group, verify not stored locally
echo -e "${GREEN}Test 3: Client sends to different group (should not be stored locally)${NC}"
./tsamclient 127.0.0.1 4029 << 'EOF'
SENDMSG,A5_30,message for other group
GETMSG
QUIT
EOF
echo ""

# Test 4: LISTSERVERS after peer connection
echo -e "${GREEN}Test 4: LISTSERVERS (with peer connected)${NC}"
./test_peer 127.0.0.1 4029 &
PEER_PID=$!
sleep 2
echo -e "LISTSERVERS\nQUIT" | ./tsamclient 127.0.0.1 4029
kill $PEER_PID 2>/dev/null
echo ""

# Test 5: FIFO order verification
echo -e "${GREEN}Test 5: FIFO message order${NC}"
./tsamclient 127.0.0.1 4029 << 'EOF'
SENDMSG,A5_29,first
SENDMSG,A5_29,second
SENDMSG,A5_29,third
GETMSG
GETMSG
GETMSG
GETMSG
QUIT
EOF
echo ""

# Cleanup
echo "Cleaning up..."
kill $SERVER_PID 2>/dev/null
pkill -9 tsamgroup29 2>/dev/null

echo ""
echo "=== Test Suite Complete ==="
echo "Server log saved to: /tmp/tsam_server.log"
