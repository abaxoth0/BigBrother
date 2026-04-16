//go:build windows

package main

import (
	"bigbrother_server_backend/cmd/app"
	"bigbrother_server_backend/packages/common/log"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/presentation/rpc"
	"bufio"
	"fmt"
	"net"
	"strings"
	"time"

	"github.com/Microsoft/go-winio"
	"github.com/abaxoth0/Ain/logger"
)

const pipeName string = `\\.\pipe\BigBrother`
const pipeBufSize int32 = 1 << 16

const (
	statusOK 	  = "1"
	statusCLIENTS = "2"
	statusSTATUS  = "3"
)

var rpcServer = rpc.NewServer()

var mainLogger = logger.NewSource("MAIN", log.DefaultLogger)

func main() {
	log.DefaultLoggerConfig.Trace = true
	log.DefaultLoggerConfig.Debug = true

	app.StartInit()
		app.InitDefaults()
	app.EndInit()

	go func() {
		if err := log.DefaultLogger.Start(); err != nil {
			panic(err.Error())
		}
	}()
	defer func() {
		if err := log.DefaultLogger.Stop(true); err != nil {
			mainLogger.Error("Failed to stop logger", err.Error(), nil)
		}
	}()

	// Reserve some time for logger to start up
	time.Sleep(time.Millisecond * 50)

	if err := sqlite.Test(); err != nil {
		panic(err)
	}

	fmt.Println("TEST: OK")
	return

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

		msg := strings.Split(line, ":")
		if len(msg) > 2 {
			fmt.Fprintf(conn, "ERROR: Invalid request syntax (':' duplication)\n")
			return
		}
		cmd, arg := msg[0], ""
		if len(msg) > 1 {
			arg = strings.TrimSpace(msg[1])
		}

		switch cmd {
		case "GET_WHITELIST":
			wl := rpcServer.GetWhitelist()

			fmt.Fprintln(conn, "WHITELIST")
			for _, entry := range wl {
				fmt.Fprintln(conn, entry)
			}

		case "REGISTER":
			if strings.TrimSpace(arg) == "" {
				fmt.Fprintf(conn, "ERROR: Missing name\n")
				return
			}
			err := rpcServer.AddClient(&rpc.Client{
				Addr: conn.RemoteAddr().String(),
				Name: arg,
				LastSeen: time.Now(),
			})
			if err != nil {
				fmt.Fprintf(conn, "ERROR: %s\n", err.Error())
				return
			}
			fmt.Fprintf(conn, statusOK)

		case "GET_CLIENTS":
			clients := rpcServer.GetClients()
			fmt.Fprintln(conn, statusCLIENTS)
			for _, client := range clients {
				fmt.Fprintf(conn, "%s:%s\n", client.Name, client.Addr)
			}

		case "GET_STATUS":
			status := rpcServer.GetStatus()
			fmt.Fprintln(conn, statusSTATUS)
			fmt.Fprintln(conn, "uptime:"+status.Uptime.String())
			for _, client := range status.ConnectedClients {
				fmt.Fprintf(conn, "%s:%s:%s\n", client.Name, client.Addr, client.LastSeen.Format(time.RFC3339))
			}
		// TODO: Implement domain addition/deletion later, for now avoid this to ensure Consistency

		default:
			fmt.Fprintf(conn, "ERROR: unknown command: %s\n", line)
		}
	}
}
