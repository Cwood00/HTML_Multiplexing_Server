# Basic HTML 1.1 server implemented in C

Server uses the port number from port.txt to accept clients and handle requests.  
Handles the following HTTP 1.1 requests ping, echo, write, read, stats, and generic get requests.  
Uses I/O multiplexing to handle multiple clinents concurrently.
