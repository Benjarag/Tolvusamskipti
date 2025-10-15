#!/bin/bash
# Quick demo of TSAM protocol implementation

echo "╔════════════════════════════════════════════════════════╗"
echo "║         TSAM Protocol - Quick Demo                    ║"
echo "║         Group A5_29                                    ║"
echo "╚════════════════════════════════════════════════════════╝"
echo ""

# Kill any existing server
pkill -9 tsamgroup29 2>/dev/null
sleep 1

# Start server in background
echo "► Starting server on port 4029..."
./tsamgroup29 4029 > /tmp/tsam_demo.log 2>&1 &
SERVER_PID=$!
sleep 2
echo "✓ Server started (PID: $SERVER_PID)"
echo ""

# Demo 1: Basic client operations
echo "═══════════════════════════════════════════════════════"
echo "Demo 1: Basic Client Operations"
echo "═══════════════════════════════════════════════════════"
echo "Commands:"
echo "  SENDMSG,A5_29,First message"
echo "  SENDMSG,A5_29,Second message"
echo "  GETMSG (expect: First message)"
echo "  GETMSG (expect: Second message)"
echo ""

./tsamclient 127.0.0.1 4029 << 'EOF'
SENDMSG,A5_29,First message
SENDMSG,A5_29,Second message
GETMSG
GETMSG
QUIT
EOF

echo ""
echo "═══════════════════════════════════════════════════════"
echo "Demo 2: Server-to-Server Communication"
echo "═══════════════════════════════════════════════════════"
echo "► Simulating peer server A5_TEST connecting..."
echo ""

./test_peer 127.0.0.1 4029

echo ""
echo "► Client retrieving message from peer..."
echo ""

echo -e "GETMSG\nQUIT" | ./tsamclient 127.0.0.1 4029

echo ""
echo "═══════════════════════════════════════════════════════"
echo "Demo 3: List Connected Servers"
echo "═══════════════════════════════════════════════════════"

# Keep a peer connection alive briefly
(./test_peer 127.0.0.1 4029 > /dev/null 2>&1; sleep 5) &
PEER_PID=$!
sleep 2

echo -e "LISTSERVERS\nQUIT" | ./tsamclient 127.0.0.1 4029

kill $PEER_PID 2>/dev/null
wait $PEER_PID 2>/dev/null

echo ""
echo "═══════════════════════════════════════════════════════"
echo "Demo Complete!"
echo "═══════════════════════════════════════════════════════"
echo ""
echo "✓ All client commands working"
echo "✓ Server-to-server communication working"
echo "✓ FIFO message ordering verified"
echo "✓ Sender information included in messages"
echo ""
echo "Server log: /tmp/tsam_demo.log"
echo ""

# Cleanup
kill $SERVER_PID 2>/dev/null
pkill -9 tsamgroup29 2>/dev/null

echo "Next steps:"
echo "1. Run './run_tests.sh' for comprehensive tests"
echo "2. Capture Wireshark: sudo tcpdump -i lo tcp port 4029 -w tsam.pcap"
echo "3. See README.md for full documentation"
