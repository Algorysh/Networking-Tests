# Networking-Tests

## What is this?
A simple framework to test network protocols for Speed (Latency and Thrpt) and Reliability. It currently tests TCP vs UDP vs QUIC.
## How to do it?
run `make` and then `make test`
It'll automatically run the test, gradually scaling the load to 5000 simultaneous clients and save the results in `results/`
## What do I need to run it?
Linux. Linux is all you need.
This uses epoll to monitor clients, which is a Linux kernel feature.
## Why was this made?
So we were tasked with testing different network protocols to optimise transfers within, to and from a trade data engine. This is not the final implementation, just some experiments.
## License and attribution?
MIT. Made by @Algorysh and @manogyasingh
