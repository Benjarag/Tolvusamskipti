# TSAM Protocol Summary

## Client Commands (sent by tsamclient to server)

| Command | Format | Response |
|---------|--------|----------|
| GETMSG | `GETMSG` | `MESSAGE: <msg>` or `NO_MESSAGES` |
| SENDMSG | `SENDMSG,GROUP_ID,<message>` | `MESSAGE_SENT` |
| LISTSERVERS | `LISTSERVERS` | `SERVERS,<group1>,<group2>,...` |

## Server-to-Server Commands

| Command | Format | Response | Notes |
|---------|--------|----------|-------|
| HELO | `HELO,<FROM_GROUP_ID>` | `SERVERS,<group>,<ip>,<port>;...` | First message on connect |
| SERVERS | `SERVERS,<group>,<ip>,<port>;...` | None | Response to HELO |
| SENDMSG | `SENDMSG,<TO_GROUP>,<FROM_GROUP>,<msg>` | None | 3-parameter format |
| KEEPALIVE | `KEEPALIVE,<msg_count>` | None | Max once/minute |
| GETMSGS | `GETMSGS,<GROUP_ID>` | `MESSAGE: <msg>` or `NO_MESSAGES` | Get messages for group |
| STATUSREQ | `STATUSREQ` | `STATUSRESP,<group>,<count>,...` | Request status |
| STATUSRESP | `STATUSRESP,<group>,<count>,...` | None | Status response |

## Key Differences

### Client SENDMSG vs Server SENDMSG
- **Client**: `SENDMSG,GROUP_ID,<message>` (2 commas)
- **Server**: `SENDMSG,TO_GROUP,FROM_GROUP,<message>` (3 commas)

### Message Flow Example
1. Client sends: `SENDMSG,A5_30,hello`
2. Server A5_29 receives, converts to: `SENDMSG,A5_30,A5_29,hello`
3. Server A5_29 forwards server format to peer servers
4. Server A5_30 receives and stores message
5. Client on A5_30 runs: `GETMSG` → receives `MESSAGE: hello`

## Wire Protocol (Framing)
All messages use binary framing:
```
[SOH(0x01)][Length_Hi][Length_Lo][STX(0x02)][Payload...][ETX(0x03)]
```
- Length is 2-byte big-endian total frame size
- Payload is ASCII command text
