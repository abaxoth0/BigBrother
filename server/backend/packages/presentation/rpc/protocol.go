package rpc

import (
	"bufio"
	"fmt"
	"net"
	"strconv"
	"strings"
)

// Status constants
const (
	StatusOK    = "OK"
	StatusError = "ERROR"
)

// writeStatus writes the status line
func writeStatus(conn net.Conn, status string) error {
	return writeLine(conn, status)
}

// writeOK writes "OK\n" - convenience
func writeOK(conn net.Conn) error {
	return writeLine(conn, StatusOK)
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

// readTLV reads a TLV: reads len line, then value line
func readTLV(scanner *bufio.Scanner) (string, error) {
	if !scanner.Scan() {
		return "", fmt.Errorf("unexpected end of input reading length")
	}
	lenStr := scanner.Text()
	expectedLen, err := strconv.Atoi(lenStr)
	if err != nil {
		return "", fmt.Errorf("invalid TLV length: %s", lenStr)
	}

	if !scanner.Scan() {
		return "", fmt.Errorf("unexpected end of input reading value")
	}
	value := scanner.Text()
	if len(value) != expectedLen {
		return "", fmt.Errorf("TLV length mismatch: expected %d, got %d", expectedLen, len(value))
	}
	return value, nil
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

		if !scanner.Scan() {
			return "", nil, fmt.Errorf("unexpected end of input for TLV value")
		}
		value := scanner.Text()
		if len(value) != expectedLen {
			return "", nil, fmt.Errorf("TLV length mismatch: expected %d, got %d", expectedLen, len(value))
		}
		args = append(args, value)
	}

	return cmd, args, nil
}

// writeLine writes a line with newline
func writeLine(conn net.Conn, s string) error {
	_, err := fmt.Fprintln(conn, s)
	return err
}
