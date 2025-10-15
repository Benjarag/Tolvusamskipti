## CONNECT Command - Quick Test

### Test with Instructor Server
```bash
./tsamclient 127.0.0.1 4029
> CONNECT 130.208.246.98 4000
> LISTSERVERS
> QUIT
```

### Test with Local Peer
```bash
# Terminal 1: Start first server on 4029
./tsamgroup29 4029

# Terminal 2: Start second server on 4030
./tsamgroup29 4030

# Terminal 3: Client connects to first server and tells it to connect to second
./tsamclient 127.0.0.1 4029
> CONNECT 127.0.0.1 4030
> LISTSERVERS
> QUIT
```

### What Happens
1. Client sends: `CONNECT <ip> <port>`
2. Server initiates outbound connection to specified peer
3. Server sends: `HELO,A5_29`
4. Peer responds with: `SERVERS,<group>,<ip>,<port>;...`
5. If peer also sends `HELO,<their_group>`, server responds with `SERVERS` list
6. Connection stays open for ongoing communication

### Expected Server Log
```
[timestamp] Attempting to connect to peer <ip>:<port>
[timestamp] Connected to peer <ip>:<port>
[timestamp] Sent to peer: HELO,A5_29
[timestamp] Received from peer <ip>:<port>: SERVERS,...
[timestamp] Peer sent SERVERS list: SERVERS,...
```

### Client Commands Now Include
- `GETMSG` - Get messages for your group
- `SENDMSG,GROUP_ID,<message>` - Send message to a group
- `LISTSERVERS` - List connected peer servers
- `CONNECT <ip> <port>` - Tell server to connect to another server (NEW!)
- `HELP` - Show commands
- `QUIT` - Exit
