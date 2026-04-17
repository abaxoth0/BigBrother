//go:build windows

package rpc

import (
	"bufio"
	"fmt"
	"net"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/Microsoft/go-winio"
	"github.com/abaxoth0/Ain/errs"
)

const backendPipeName 	  = `\\.\pipe\BigBrother.Server.Backend`
const backendPipeBufSize = 65536

const (
	StatusOK = 1 + iota
	StatusClients
	StatusServerStatus
)

const DefaultHandlerStopTimeout = time.Second * 10

type Handler interface {
	Start() error
	Stop(timeout time.Duration)  error
	handle(conn net.Conn)

	Server
}

// Unlike in default request-response model, this handler allows users to call server and server to call users.
// So this app can also call RPCs but on client side, which makes this interaction more similar to P2P model.
type DuplexHandler struct {
	*DuplexServer

	doneCh chan struct{}
	stopCh chan struct{}
}

func NewDuplexHandler() *DuplexHandler {
	return &DuplexHandler{
		DuplexServer: new(DuplexServer),
		doneCh: make(chan struct{}),
		stopCh: make(chan struct{}),
	}
}

func (h *DuplexHandler) Start() error {
	defer close(h.stopCh)

	cfg := &winio.PipeConfig{
		MessageMode:      true,
		InputBufferSize:  backendPipeBufSize,
		OutputBufferSize: backendPipeBufSize,
	}
	listener, err := winio.ListenPipe(backendPipeName, cfg)
	if err != nil {
		return err
	}
	defer listener.Close()

	log.Info("Server listening on: "+backendPipeName, nil)
	var wg sync.WaitGroup

	for {
		select {
		case <-h.doneCh:
			wg.Wait()
			return nil
		default:
			for {
				conn, err := listener.Accept()
				if err != nil {
					log.Error("Accept error", err.Error(), nil)
					continue
				}

				go func() {
					wg.Add(1)
					defer wg.Done()
					h.handle(conn)
				} ()
			}
		}
	}
}

func (h *DuplexHandler) Stop(timeout time.Duration) error {
	if timeout == 0 {
		timeout = DefaultHandlerStopTimeout
	}
	close(h.doneCh)
	select {
	case <-h.stopCh:
		return nil
	case <-time.After(timeout):
		return errs.StatusTimeout
	}
}

type errorType int

const (
	internalError errorType = iota
	requestError
)

func write(conn net.Conn, format string, a ...any) error {
	addrStr := conn.RemoteAddr().String()
	log.Trace("Writting to \""+addrStr+"\": \""+fmt.Sprintf(format, a...)+"\"...", nil)
	if _, err := fmt.Fprintf(conn, format, a...); err != nil {
		log.Error("Connection write error", err.Error(), nil)
		return err
	}
	log.Trace(fmt.Sprintf("Writting to \"%s\": OK", addrStr), nil)
	return nil
}

func writeln(conn net.Conn, buf ...any) error {
	addrStr := conn.RemoteAddr().String()
	log.Trace("Writting to \""+addrStr+"\": \""+fmt.Sprintln(buf...)+"\"...", nil)
	if _, err := fmt.Fprintln(conn, buf...); err != nil {
		log.Error("Connection write error", err.Error(), nil)
		return err
	}
	log.Trace(fmt.Sprintf("Writting to \"%s\": OK", addrStr), nil)
	return nil
}

func writeError(conn net.Conn, _type errorType, msg string) {
	var logMsg string
	switch _type {
	case internalError:
		logMsg = "Internal Error"
	case requestError:
		logMsg = "Request Error"
	default:
		panic("unknown error type: " + strconv.Itoa(int(_type)))
	}
	log.Error(logMsg, msg, nil)
	write(conn, "ERROR: "+msg+"\n")
}

func (h *DuplexHandler) handle(conn net.Conn) {
	defer conn.Close()
	log.Info("Client connected", nil)

	scanner := bufio.NewScanner(conn)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}

		log.Debug("Received: "+line, nil)

		msg := strings.Split(line, ":")
		if len(msg) > 2 {
			writeError(conn, requestError, "Invalid request syntax (':' duplication)")
			return
		}
		cmd, arg := msg[0], ""
		if len(msg) > 1 {
			arg = strings.TrimSpace(msg[1])
		}

		switch cmd {
		case "GET_WHITELIST":
			wl := h.GetWhitelist(arg)

			writeln(conn, "WHITELIST")
			for _, entry := range wl {
				writeln(conn, entry)
			}

		case "REGISTER":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing name")
				return
			}
			if err := h.RegisterUser(conn.RemoteAddr().String(), arg); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "%d", StatusOK)

		case "GET_CLIENTS":
			connections := h.GetConnections()
			writeln(conn, StatusClients)
			for _, connection := range connections {
				client := connection.GetUser()
				write(conn, "%s:%s\n", client.Name, client.Addr)
			}

		case "GET_STATUS":
			status := h.GetStatus()
			writeln(conn, StatusServerStatus)
			writeln(conn, "uptime:"+status.Uptime.String())
			for _, connection := range status.Connections {
				client := connection.GetUser()
				write(conn, "%s:%s:%s\n", client.Name, client.Addr)
			}

		default:
			fmt.Fprintf(conn, "ERROR: unknown command: %s\n", line)
		}
	}
}
