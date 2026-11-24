package rpc

import (
	"net"
	"strconv"
	"strings"
	"testing"
)

// helper: writes a framed request to one end, reads via newScanner from the other.
func runReadRequest(t *testing.T, request string) (string, []string, error) {
	t.Helper()
	server, client := net.Pipe()
	defer server.Close()
	defer client.Close()

	go func() {
		client.Write([]byte(request))
		client.Close()
	}()

	scanner := newScanner(server)
	return readRequest(scanner)
}

func TestReadRequestBasic(t *testing.T) {
	cmd, args, err := runReadRequest(t, "PING\n\n")
	if err != nil {
		t.Fatalf("readRequest: %v", err)
	}
	if cmd != "PING" || len(args) != 0 {
		t.Fatalf("unexpected result: cmd=%q args=%v", cmd, args)
	}
}

func TestReadRequestWithArgs(t *testing.T) {
	cmd, args, err := runReadRequest(t, "SAVE_WHITELIST\n4\nmain\n3\na.b\n\n")
	if err != nil {
		t.Fatalf("readRequest: %v", err)
	}
	if cmd != "SAVE_WHITELIST" {
		t.Fatalf("unexpected cmd %q", cmd)
	}
	if len(args) != 2 || args[0] != "main" || args[1] != "a.b" {
		t.Fatalf("unexpected args %v", args)
	}
}

func TestReadRequestLengthMismatch(t *testing.T) {
	_, _, err := runReadRequest(t, "FOO\n5\nabc\n\n")
	if err == nil || !strings.Contains(err.Error(), "length mismatch") {
		t.Fatalf("expected length mismatch error, got %v", err)
	}
}

func TestReadRequestEmpty(t *testing.T) {
	_, _, err := runReadRequest(t, "\n\n")
	if err == nil || !strings.Contains(err.Error(), "empty request") {
		t.Fatalf("expected empty request error, got %v", err)
	}
}

func TestReadRequestLargeValue(t *testing.T) {
	// A whitelist payload that exceeds the Scanner default 64KB cap proves the
	// Buffer config on newScanner is actually in effect.
	big := strings.Repeat("x", 100*1024)
	request := "SAVE_WHITELIST\n" + strconv.Itoa(len(big)) + "\n" + big + "\n\n"
	cmd, args, err := runReadRequest(t, request)
	if err != nil {
		t.Fatalf("readRequest with large value: %v", err)
	}
	if cmd != "SAVE_WHITELIST" || len(args) != 1 || len(args[0]) != len(big) {
		t.Fatalf("large value not round-tripped: cmd=%q args=%d", cmd, len(args))
	}
}
