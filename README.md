# Networking-Tests

## What is this?
A simple framework to test network protocols for Speed (Latency and Thrpt) and Reliability.
## How to do it?
run `make` and then `make test`
It'll automatically run the test, gradually scaling the load to 5000 simultaneous clients and save the results in `results/`
## What do I need to run it?
Linux. Linux is all you need.
This uses epoll to monitor clients, which is a Linux kernel feature.
## License and attribution?
MIT. 
