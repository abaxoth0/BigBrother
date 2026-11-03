//go:build windows

package rpc

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/infrastructure/notification"
)

func TestDisconnectClosesOnlySelectedClientSubscription(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "disconnect.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	manager := connection.NewMemoryResidentConnectionManager()
	defer manager.Stop()
	bus := notification.NewManager()
	backend := NewBackendHandler(db, manager, nil, bus)
	frontend := NewFrontendHandler(db, manager, nil, bus)
	for _, name := range []string{"alice", "bob"} {
		if _, err := db.CreateUser(name, name+".test"); err != nil {
			t.Fatal(err)
		}
		if err := db.SetUserToken(name, "token"); err != nil {
			t.Fatal(err)
		}
		if err := backend.ConnectUser(name, name+".test"); err != nil {
			t.Fatal(err)
		}
	}
	type subscription struct {
		conn   net.Conn
		reader *bufio.Reader
		done   chan struct{}
	}
	subscribe := func(name string) subscription {
		t.Helper()
		server, client := net.Pipe()
		t.Cleanup(func() { client.Close() })
		client.SetDeadline(time.Now().Add(5 * time.Second))
		done := make(chan struct{})
		go func() { backend.handle(server); close(done) }()
		fmt.Fprintf(client, "SUBSCRIBE\n%d\n%s\n5\ntoken\n\n", len(name), name)
		reader := bufio.NewReader(client)
		if status, err := reader.ReadString('\n'); err != nil || status != "OK\n" {
			t.Fatalf("subscribe %s: %q, %v", name, status, err)
		}
		reader.ReadString('\n')
		return subscription{client, reader, done}
	}
	alice := subscribe("alice")
	bob := subscribe("bob")
	old, _ := manager.GetConnection("alice")
	if got := handlerResponse(t, frontend, "DISCONNECT\n5\nalice\n\n"); got != "OK\n\n" {
		t.Fatalf("disconnect: %q", got)
	}
	raw, err := io.ReadAll(alice.reader)
	if err != nil || string(raw) != "EVENT\n19\nCLIENT_DISCONNECTED\n10\nname=alice\n\n" {
		t.Fatalf("client retained event stream or missed disconnect: %q, %v", raw, err)
	}
	select {
	case <-alice.done:
	case <-time.After(time.Second):
		t.Fatal("disconnected handler remained running")
	}
	if _, err := manager.GetConnection("alice"); err != connection.ErrConnectionNotFound {
		t.Fatal("disconnected client remained listed")
	}
	if _, err := manager.GetConnection("bob"); err != nil {
		t.Fatal("other client disconnected")
	}
	bus.Publish(notification.Event{Type: notification.WhitelistChanged})
	if got, err := bob.reader.ReadString('\n'); err != nil || got != "EVENT\n" {
		t.Fatalf("other client missed updates: %q, %v", got, err)
	}
	if n, err := bob.reader.ReadString('\n'); err != nil || n != "17\n" {
		t.Fatalf("whitelist event length: %q, %v", n, err)
	}
	if event, _ := bob.reader.ReadString('\n'); event != "WHITELIST_CHANGED\n" {
		t.Fatalf("wrong event: %q", event)
	}
	bob.reader.ReadString('\n')
	reconnected := subscribe("alice")
	current, err := manager.GetConnection("alice")
	if err != nil || current.ID() == old.ID() {
		t.Fatal("reconnect did not create a fresh session")
	}
	manager.DeleteSession("alice", old.ID())
	if got := handlerResponse(t, backend, "DISCONNECT\n5\nalice\n5\ntoken\n\n"); got != "OK\n\n" {
		t.Fatalf("client disconnect: %q", got)
	}
	raw, err = io.ReadAll(reconnected.reader)
	if err != nil || strings.Contains(string(raw), "CLIENT_DISCONNECTED") {
		t.Fatalf("normal disconnect was treated as administrative: %q, %v", raw, err)
	}
	select {
	case <-reconnected.done:
	case <-time.After(time.Second):
		t.Fatal("client disconnect left subscription open")
	}
}
