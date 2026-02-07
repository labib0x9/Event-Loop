package main

import (
	"fmt"
	"net"
	"os"
	"sync"
	"time"
)

func main() {
	if len(os.Args) != 4 {
		fmt.Println("usage: client <host:port> <clients> <mode>")
		fmt.Println("mode: idle | slow | spam")
		os.Exit(1)
	}

	addr := os.Args[1]
	n := atoi(os.Args[2])
	mode := os.Args[3]

	var wg sync.WaitGroup
	wg.Add(n)

	for i := 0; i < n; i++ {
		go func(id int) {
			defer wg.Done()

			conn, err := net.Dial("tcp", addr)
			if err != nil {
				fmt.Println("dial error:", err)
				return
			}
			defer conn.Close()

			switch mode {
			case "idle":
				select {} // stay open forever

			case "slow":
				for {
					_, err := conn.Write([]byte("x"))
					if err != nil {
						return
					}
					time.Sleep(5 * time.Second)
				}

			case "spam":
				for {
					_, err := conn.Write([]byte("hello\n"))
					if err != nil {
						return
					}
				}
			}
		}(i)
	}

	wg.Wait()
}

func atoi(s string) int {
	var n int
	fmt.Sscan(s, &n)
	return n
}
