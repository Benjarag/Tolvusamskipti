# README  

## Project Overview  
This project implements a client/server protocol for peer-to-peer communication. The submission includes the source code, a Makefile for compilation, and additional files as required.  

## Submission Contents  
- **Source Code**: Client and server implementation.  
- **Makefile**: For compiling the project.  
- **Wireshark Trace**: Captures for the client-server communication.  
- **Log Files**: Relevant logs for debugging and demonstration.  

# README

## Project overview
This repository contains a simple TSAM client/server implementation (group 29) used for the course assignment. It includes the server and client source, a Makefile to build the project, a packet capture of a recorded session, and example logs showing a short interaction where the client sends and retrieves messages.

## Submission contents
- Source code: C++ sources for client, server and networking/protocol helpers.
- Build: `Makefile` to compile the project (produces the server and client executables used below).
- Packet capture: `tsam_capture.pcap` — recorded with `tcpdump` (see note below).
- Example logs: example command output from a recorded session (included in this README).

## Claimed bonus
### Early submission (10 points)
- Early submission claim: a working client and server were implemented and a short demo session was recorded. The capture file is included.

## Build
Run in the project root directory:

```bash
make
```

The build in this workspace produces the client and server binaries used in the recorded session:

- Server executable: `tsamgroup29`
- Client executable: `tsamclient`

## Run / reproduce the recorded session
1. Start the server on port 4029:

```bash
./tsamgroup29 4029
```

You should see output similar to:

[2025-10-13 16:56:00] Server A5_29 listening on port 4029

2. Start the client and connect to the server:

```bash
./tsamclient 127.0.0.1 4029
```

Client output for the recorded session (abridged):

[2025-10-13 16:56:02] Connected to server 127.0.0.1:4029

Client commands available in this implementation:

- SENDMSG,GROUP_ID,<message> — send a message to the given group (comma-separated)
- GETMSG — retrieve the next message for the client group
- LISTSERVERS — request a list of servers
- HELP — show the help text
- QUIT — exit the client

Example interaction (abridged):

tsamgroup29> GETMSG
[2025-10-13 16:56:05] Server response: NO_MESSAGES

tsamgroup29> LISTSERVERS
[2025-10-13 16:56:09] Server response: SERVERS

tsamgroup29> SENDMSG,A5_29,Hello message nr 1
[2025-10-13 16:56:55] Server response: MESSAGE_SENT

... (two more SENDMSG calls, then three GETMSG calls returning the three messages) ...

Client disconnect:

[2025-10-13 16:57:24] Disconnected from server

Server-side log excerpt from the same session (abridged):

[2025-10-13 16:56:02] New connection accepted from 127.0.0.1:59306
[2025-10-13 16:56:05] Connection identified as client on fd=4
[2025-10-13 16:56:05] Sent to client: NO_MESSAGES
[2025-10-13 16:56:55] Received from connection client SENDMSG,A5_29,Hello message nr 1
[2025-10-13 16:56:55] Stored message from client for group A5_29: Hello message nr 1
[2025-10-13 16:56:55] Sent reply: MESSAGE_SENT

... (additional SENDMSG/GETMSG handling) ...

[2025-10-13 16:57:24] Connection disconnected fd=4

## Packet capture (pcap)
The included packet capture is `tsam_capture.pcap`. Note: the capture in this workspace was recorded using `tcpdump` (not live from Wireshark). The command used to produce the file was:

```bash
sudo tcpdump -i any tcp port 4029 -w tsam_capture.pcap
```

This produced a capture containing the 31 packets from the demo session and can be opened in Wireshark for inspection.

Summary from the tcpdump run in this session:

tcpdump: listening on any, link-type LINUX_SLL2 (Linux cooked v2)
31 packets captured
62 packets received by filter
0 packets dropped by kernel

## Development environment
- OS used for development and testing: Ubuntu 22.04
- Compiler: GCC 11.3 (used by the environment where the binary was built)

## Files in this repository (not exhaustive)
- `client.cpp` — client implementation and user CLI
- `server.cpp` — server main and accept/dispatch logic
- `networking.cpp`, `networking.h` — network helpers / read/write utilities
- `protocol.cpp`, `protocol.h` — protocol helpers (message formatting/parsing)
- `Makefile` — build instructions
- `tsam_capture.pcap` — packet capture recorded with tcpdump
- `tsamclient` — built client executable (example)
- `tsamgroup29` — built server executable (example)
- `run_demo.sh` — optional helper script to build the project, run a short scripted client session and save `client_log.txt` and `server_log.txt` (capture is disabled by default; use `--capture` to enable tcpdump)
- `client_log.txt` — example full client transcript from a scripted demo
- `server_log.txt` — example full server transcript from the same demo

If any filenames above differ in your workspace (for example different source file extensions or binary names), use `ls` to list the directory and adapt the commands accordingly.

## Notes
- The packet capture was recorded with `tcpdump` (command shown above) and can be opened with Wireshark for analysis.

LISTSERVERS instruction
The LISTSERVERS command that is included in the trace, currently returns only SERVERS due to no peers being connected to the server at that time. 
See below:

tsamgroup29> LISTSERVERS
[2025-10-13 16:56:09] Server response: SERVERS
