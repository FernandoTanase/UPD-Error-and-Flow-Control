# UPD-Error-and-Flow-Control
This project implements error control and flow control mechanisms on top of a client-server application using UDP sockets. The implementation includes:  UDP-based "Stop-and-Wait" Protocol, and Sequence Numbers (SN) &amp; Automatic Repeat Request (ARQ) for reliable data transfer.

# Files
## pa2_code.c (TASK 1)
- Basic UDP implementation that does suffer packet loss.
- Compile: ```gcc -o pa2_binary pa2_code.c -pthread```
- Terminal #1 (server): ```./pa2_binary server 127.0.0.1 12345```
- Terminal #2 (client): ```./pa2_binary client  127.0.0.1 12345 8 1000000```

## pa2_part2.c (TASK 2)
- Addition of equence numbers & ARQ implementation.
- Compile: ```gcc -o pa2_binary pa2_part2.c -pthread```
- Terminal #1 (server): ```./pa2_binary server 127.0.0.1 12345```
- Terminal #2 (client): ```./pa2_binary client  127.0.0.1 12345 8 1000000```
