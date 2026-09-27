package rpc

import (
	"bufio"
	"fmt"
	"net"
	"strconv"
	"strings"
	"time"
)

// Status constants
const (
	StatusOK    = "OK"
	StatusError = "ERROR"
)

// maxMessageSize caps a single TLV value (whitelist dumps can be large).
const maxMessageSize = 4 * 1024 * 1024

// requestReadTimeout bounds how long a peer may sit idle between commands so
// a dead or malicious client can't pin a handler goroutine forever.
// SUBSCRIBE handlers clear the deadline (they wait for pushed events/heartbeats).
const requestReadTimeout = 60 * time.Second

// newScanner creates a Scanner large enough to hold the largest TLV value.
func newScanner(conn net.Conn) *bufio.Scanner {
	s := bufio.NewScanner(conn)
	s.Buffer(make([]byte, 64*1024), maxMessageSize)
	return s
}

// writeOK writes "OK\n\n" - convenience (empty line terminates response)
func writeOK(conn net.Conn) error {
	if err := writeLine(conn, StatusOK); err != nil {
		return err
	}
	return writeLine(conn, "") // empty line terminates response
}

// writeErrorTLV writes "ERROR\n<len>\n<msg>\n" in TLV format
func writeErrorTLV(conn net.Conn, msg string) error {
	if err := writeLine(conn, StatusError); err != nil {
		return err
	}
	if err := writeTLV(conn, msg); err != nil {
		return err
	}
	return writeLine(conn, "") // empty line terminates response
}

// writeTLV writes a single TLV: <len>\n<value>\n
func writeTLV(conn net.Conn, value string) error {
	if err := writeLine(conn, strconv.Itoa(len(value))); err != nil {
		return err
	}
	return writeLine(conn, value)
}

// writeTLVResponse writes OK status + data lines in TLV format
func writeTLVResponse(conn net.Conn, dataLines ...string) error {
	if err := writeLine(conn, StatusOK); err != nil {
		return err
	}
	for _, line := range dataLines {
		if err := writeTLV(conn, line); err != nil {
			return err
		}
	}
	return writeLine(conn, "") // empty line terminates response
}

// readRequest reads command and TLV arguments until empty line
func readRequest(scanner *bufio.Scanner) (cmd string, args []string, err error) {
	// First non-empty line is command
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line != "" {
			cmd = line
			break
		}
	}
	if err := scanner.Err(); err != nil {
		return "", nil, fmt.Errorf("reading command: %w", err)
	}
	if cmd == "" {
		return "", nil, fmt.Errorf("empty request")
	}

	// Read TLV arguments until empty line
	for scanner.Scan() {
		line := scanner.Text()
		if line == "" {
			break // empty line terminates request
		}

		// line should be the length
		expectedLen, err := strconv.Atoi(line)
		if err != nil {
			return "", nil, fmt.Errorf("invalid TLV length: %s", line)
		}
		if expectedLen < 0 {
			return "", nil, fmt.Errorf("invalid TLV length: %s", line)
		}

		if !scanner.Scan() {
			if err := scanner.Err(); err != nil {
				return "", nil, fmt.Errorf("reading TLV value: %w", err)
			}
			return "", nil, fmt.Errorf("unexpected end of input for TLV value")
		}
		value := scanner.Text()
		if len(value) != expectedLen {
			return "", nil, fmt.Errorf("TLV length mismatch: expected %d, got %d", expectedLen, len(value))
		}
		args = append(args, value)
	}
	if err := scanner.Err(); err != nil {
		return "", nil, fmt.Errorf("reading TLV argument: %w", err)
	}

	return cmd, args, nil
}

// writeLine writes a line with newline
func writeLine(conn net.Conn, s string) error {
	_, err := fmt.Fprintln(conn, s)
	return err
}
