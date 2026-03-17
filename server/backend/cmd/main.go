//go:build windows

package main

import (
	"bufio"
	"fmt"
	"net"
	"strings"

	"github.com/Microsoft/go-winio"
)

const pipeName string = `\\.\pipe\BigBrother`
const pipeBufSize int32 = 1 << 16

func main() {
	cfg := &winio.PipeConfig{
		MessageMode:      true,
		InputBufferSize:  pipeBufSize,
		OutputBufferSize: pipeBufSize,
	}
	listener, err := winio.ListenPipe(pipeName, cfg)
	if err != nil {
		panic(err)
	}
	defer listener.Close()

	fmt.Printf("Server listening on %s\n", pipeName)

	for {
		conn, err := listener.Accept()
		if err != nil {
			fmt.Printf("Accept error: %v\n", err)
			continue
		}

		go handleConnection(conn)
	}
}

func handleConnection(conn net.Conn) {
	defer conn.Close()
	fmt.Println("Client connected")

	scanner := bufio.NewScanner(conn)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}

		fmt.Printf("Received: %s\n", line)

		switch line {
		case "GET_WHITELIST":
			// TODO: Get from database
			fmt.Fprintln(conn, "WHITELIST")
			fmt.Fprintln(conn, "example.com")
			fmt.Fprintln(conn, "github.com")

		case "REGISTER":
			// TODO: Register client
			fmt.Fprintln(conn, "OK")

		case "GET_STATUS":
			// TODO: Get status
			fmt.Fprintln(conn, "STATUS")
			fmt.Fprintln(conn, "clients:0")

		default:
			fmt.Fprintf(conn, "ERROR: unknown command: %s\n", line)
		}
	}
}
