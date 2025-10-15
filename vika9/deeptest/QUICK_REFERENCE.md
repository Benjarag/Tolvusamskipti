# Quick Command Reference

## Build
```bash
make clean && make
```

## Run Server
```bash
./tsamgroup29 4029
```

## Run Client
```bash
./tsamclient 127.0.0.1 4029
```

## Client Commands (in interactive prompt)
```
SENDMSG,A5_29,your message here
GETMSG
LISTSERVERS
HELP
QUIT
```

## Test Scripts
```bash
# Quick demo
./demo.sh

# Full test suite
./run_tests.sh

# Test peer communication
./test_peer 127.0.0.1 4029
```

## Wireshark Capture
```bash
# Terminal 1: Start capture
sudo tcpdump -i lo tcp port 4029 -w capture.pcap

# Terminal 2: Run tests
./demo.sh

# Terminal 1: Stop capture (Ctrl+C), then view
wireshark capture.pcap
```

## One-Line Tests
```bash
# Send and receive
echo -e "SENDMSG,A5_29,test\nGETMSG\nQUIT" | ./tsamclient 127.0.0.1 4029

# List servers
echo -e "LISTSERVERS\nQUIT" | ./tsamclient 127.0.0.1 4029

# Multiple messages FIFO
echo -e "SENDMSG,A5_29,first\nSENDMSG,A5_29,second\nGETMSG\nGETMSG\nQUIT" | ./tsamclient 127.0.0.1 4029
```

## Debugging
```bash
# View server log
tail -f /tmp/tsam_demo.log

# Check if server is running
netstat -tuln | grep 4029
ps aux | grep tsamgroup29

# Kill server
pkill -9 tsamgroup29

# Test connection
telnet 127.0.0.1 4029
nc 127.0.0.1 4029
```

## Common Issues

**Port already in use:**
```bash
pkill -9 tsamgroup29
sleep 2
./tsamgroup29 4029
```

**Can't connect:**
```bash
# Check server is running
ps aux | grep tsamgroup29

# Check port
netstat -tuln | grep 4029
```

**Framing errors:**
- Ensure protocol.cpp byte order is correct (already fixed)
- Check length field in Wireshark (should be small, not thousands)
