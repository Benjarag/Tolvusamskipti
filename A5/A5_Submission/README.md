# TSAM Assignment — A5_29

This README describes how to build and run the server/client, which assignment and
bonus points we intend to claim, and where the timestamped evidence lives in the
repository. It also documents implemented commands and notable behaviours.

## Quick build & run

Environment: tested on Linux (Ubuntu 20.04+ / Debian-like). Compiler: g++ (C++11).

Build:

```bash
cd /home/benjaminr23/A5/
make
```

Run server (example listening port 4029):

```bash
./tsamgroup29 4029
```

Run client (included):

```bash
./tsamclient 130.208.246.130 4029
# then use client commands such as GETMSG, GETALLMSG, CONNECT, LISTSERVERS, HELP, QUIT.
```

### External/Port-Forwarded IP Configuration

For the **External Bot bonus**, the server supports advertising a public IP address different 
from the local machine IP. This enables port forwarding scenarios (e.g., running the server 
behind a NAT router or on a home network).

To advertise a custom external IP:

```bash
export TSAM_PUBLIC_IP=203.0.113.42  # Your external IP
./tsamgroup29 4029
```

The server will use this IP in all SERVERS responses sent to peers. The `Networking::advertised_ip()` 
function implements a fallback strategy:

1. **Environment variable** (`TSAM_PUBLIC_IP`) — highest priority
2. **Hostname resolution** — tries to resolve local hostname to IPv4
3. **Outgoing interface detection** — determines the IP used for external connectivity
4. **Fallback** — defaults to 127.0.0.1 if all else fails

This allows peers to connect back to your server even when running behind NAT or on external networks.

## Implemented protocol commands

Server recognises and handles the following commands (peer and client):

- HELO (peer handshake)
- SERVERS (peer list reply)
- LISTSERVERS (client request -> replies with SERVERS list)
- CONNECT (client requests an outbound connect to a peer)
- STATUSREQ / STATUSRESP
- KEEPALIVE
- SENDMSG (message framing uses SOH/STX/ETX per assignment)
- GETMSG / GETMSGS (client/peer message retrieval)

The implementation uses the framing specification described in `protocol.h` /
`protocol.cpp` (SOH, 2-byte length, STX, payload, ETX). Payload max = 5000 bytes.

**Message deduplication**: We extract hops from incoming SENDMSG (after EOT delimiter), 
deduplicate based on from/to/content only (ignoring hop variations), then forward with 
updated hops. This was implemented after we had already created a signature-based dedup 
system, before realizing hops were part of the protocol.

## What to include in the submission (assignment points)

The following documents and files are included and referenced for grading:

- `assignment_points.txt` — per-assignment notes and sample log excerpts for (a)-(g).
- `bonus_points.txt` — working notes for bonus claims.
- `Ready_Bot_log/` — directory containing per-day extracts used as evidence for ready bot bonus.
  - `12th_and18th.log`
  - `19th.log`
  - `20th.log`
  - `21st.log`
  - `22nd.log`
  - `23rd.log`
All timestamps in logs use the format: `[YYYY-MM-DD HH:MM:SS] ...`.

### Assignment mandatory points (1--g)

Look at `assignment_points.txt` for details on how each requirement is met.

## Bonus points we are claiming

Look at `bonus_points.txt` for details on how each bonus requirement is met.
We are claiming all bonuses (a)--(f).

## Notes for the grader

We changed the protocol from no hops to hops EOT on the next to last day,
so some logs may show messages without hops.

I talked to my TA about always having the Instr_1 server connected, so that
I can forward messages through it when needed. I believe this is acceptable as it allows better message delivery.
