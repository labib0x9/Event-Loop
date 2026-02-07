#!/bin/bash

echo "Testing the echo server with 5 concurrent connections..."
echo ""

# for i in {1..5}; do
#     (echo "Hello from connection $i" | nc 127.0.0.1 8080) &
# done

# wait

# Use telnet or a script that keeps connection open
for i in {1..5}; do
    (
        echo "Hello from connection $i"
        sleep 1  # Keep connection open
        echo "Goodbye"
        sleep 1
    ) | nc 127.0.0.1 8080 &
done

echo ""
echo "All connections completed"