# TSAM Botnet Protocol Implementation

## Overview
Complete implementation of TSAM (Totally Secure Application Messaging) botnet protocol for Computer Networks course assignment.

## Files
- `server.cpp` - Main server implementation with client and server-to-server protocol handlers
- `client.cpp` - Interactive client for sending/receiving messages
- `protocol.cpp` - Binary framing protocol (SOH/STX/ETX)
- `networking.cpp` - Robust TCP networking with partial read handling
- `test_peer.cpp` - Tool for testing server-to-server communication
- `run_tests.sh` - Comprehensive test suite

## Building
```bash
make clean
make
```

Produces:
- `tsamgroup29` - Server executable
- `tsamclient` - Client executable
- `test_peer` - Peer server simulator

## Running

### Start Server
```bash
./tsamgroup29 <port>
# Example:
./tsamgroup29 4029
```

### Start Client
```bash
./tsamclient <server_ip> <server_port>
# Example:
./tsamclient 127.0.0.1 4029
```

### Run Tests
```bash
./run_tests.sh
```

## Protocol Details

### Client Commands
| Command | Format | Description |
|---------|--------|-------------|
| GETMSG | `GETMSG` | Get oldest message for your group (FIFO) |
| SENDMSG | `SENDMSG,GROUP_ID,<message>` | Send message to a group |
| LISTSERVERS | `LISTSERVERS` | List connected peer servers |

### Server-to-Server Commands
| Command | Format | Description |
|---------|--------|-------------|
| HELO | `HELO,<FROM_GROUP_ID>` | Initiate peer connection |
| SERVERS | `SERVERS,<group>,<ip>,<port>;...` | Response to HELO |
| SENDMSG | `SENDMSG,<TO_GROUP>,<FROM_GROUP>,<msg>` | Forward message between servers |
| KEEPALIVE | `KEEPALIVE,<msg_count>` | Periodic heartbeat |
| GETMSGS | `GETMSGS,<GROUP_ID>` | Request messages for group |
| STATUSREQ | `STATUSREQ` | Request status |
| STATUSRESP | `STATUSRESP,<group>,<count>,...` | Status response |

### Wire Format
All messages use binary framing:
```
[SOH(0x01)][Length_Hi][Length_Lo][STX(0x02)][Payload...][ETX(0x03)]
```
- Length: 2-byte big-endian total frame size
- Payload: ASCII command text
- Max message: 5000 bytes

## Features

✅ **Client-Server Protocol**
- FIFO message queue per group
- Messages include sender information
- Proper command separation (client vs server)

✅ **Server-to-Server Protocol**
- HELO handshake with SERVERS response
- Three-parameter SENDMSG format
- Message forwarding between peers
- Connection tracking

✅ **Robust Networking**
- Binary framing protocol
- Handles partial TCP reads (`read_exact`)
- Thread-per-connection architecture
- Graceful connection handling

✅ **Message Flow**
1. Client A sends: `SENDMSG,GroupB,hello`
2. Server A converts to: `SENDMSG,GroupB,GroupA,hello`
3. Server A forwards to peer Server B
4. Server B stores message
5. Client B runs: `GETMSG` → receives `MESSAGE: From: GroupA Msg: hello`

## Testing Examples

### Basic Client Test
```bash
./tsamclient 127.0.0.1 4029
> SENDMSG,A5_29,Hello World
> GETMSG
MESSAGE: From: A5_29 Msg: Hello World
> QUIT
```

### Peer Communication Test
```bash
# Terminal 1: Start server
./tsamgroup29 4029

# Terminal 2: Simulate peer sending message
./test_peer 127.0.0.1 4029

# Terminal 3: Client retrieves message
./tsamclient 127.0.0.1 4029
> GETMSG
MESSAGE: From: A5_TEST Msg: Hello from test peer!
```

### Wireshark Capture
```bash
# Start capture
sudo tcpdump -i lo tcp port 4029 -w tsam.pcap

# Run tests (in another terminal)
./tsamclient 127.0.0.1 4029
> SENDMSG,A5_29,test
> GETMSG
> QUIT

# Stop capture (Ctrl+C)
# Open in Wireshark
wireshark tsam.pcap
```

In Wireshark:
- Filter: `tcp.port == 4029`
- Right-click packet → Follow → TCP Stream
- View as Hex Dump to see framing bytes

## Group Information
- Group ID: `A5_29`
- Server executable: `tsamgroup29`
- Ports: 4000-4200

## Assignment Requirements Met

✅ (a) 40 points - Basic client-server protocol
- GETMSG returns messages in FIFO order with sender info
- SENDMSG sends to groups and forwards to peers
- LISTSERVERS shows connected peers
- Server handles all commands via network (no user input)

✅ (b) 10 points - Wireshark traces
- Binary framing visible in captures
- Can capture all client-server commands
- Follow TCP Stream shows protocol messages

## Known Implementation Details

1. **Message Storage**: FIFO queue with sender tracking
2. **Connection Classification**: First message determines peer vs client
3. **Forwarding**: Client SENDMSG converted to 3-param server format
4. **Thread Safety**: Mutex protection for shared data structures
5. **Graceful Shutdown**: SIGPIPE ignored, connections cleaned up properly

## Troubleshooting

**"Address already in use"**
```bash
pkill -9 tsamgroup29
# Wait a few seconds, then retry
```

**Length framing errors**
- Ensure protocol.cpp uses correct byte order (big-endian)
- Check no double byte-swapping (ntohs on already-swapped values)

**Client freezing**
- Verify server is running: `netstat -tuln | grep 4029`
- Check firewall rules if connecting remotely

## Future Enhancements (Optional)

- [ ] Persistent message storage (disk)
- [ ] Message timestamps
- [ ] Authentication/encryption
- [ ] Multi-group message routing
- [ ] Automatic peer discovery
- [ ] Status monitoring dashboard
