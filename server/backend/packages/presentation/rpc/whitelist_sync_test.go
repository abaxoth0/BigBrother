//go:build windows

package rpc

import (
	"bufio"
	"errors"
	"net"
	"path/filepath"
	"strings"
	"testing"
	"time"

	whitelistapplication "bigbrother_server_backend/packages/application/whitelist"
	"bigbrother_server_backend/packages/domain/entity"
	"bigbrother_server_backend/packages/infrastructure/database"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/infrastructure/notification"
)

func TestWhitelistSyncRequest(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "sync.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	if _, err := db.CreateUser("eve", "10.0.0.2"); err != nil {
		t.Fatal(err)
	}
	if err := db.SetUserToken("eve", "token"); err != nil {
		t.Fatal(err)
	}
	bus := notification.NewManager()
	frontend := NewFrontendHandler(db, nil, nil, bus)
	backend := NewBackendHandler(db, nil, nil, bus)
	server, client := net.Pipe()
	defer client.Close()
	client.SetDeadline(time.Now().Add(5 * time.Second))
	sub := notification.NewSubscriber(server, 4096)
	if err := bus.Add(sub); err != nil {
		t.Fatal(err)
	}
	defer bus.Remove(sub)
	reader := bufio.NewReader(client)
	check := func(revision string) {
		t.Helper()
		if got := handlerResponse(t, frontend, "SYNC_WHITELISTS\n\n"); got != "OK\n\n" {
			t.Fatalf("sync request: %q", got)
		}
		for _, expected := range []string{"EVENT\n", "24\n", "WHITELIST_SYNC_REQUESTED\n", "73\n", "revision=" + revision + "\n", "\n"} {
			if got, err := reader.ReadString('\n'); err != nil || got != expected {
				t.Fatalf("sync event: got %q, %v; want %q", got, err, expected)
			}
		}
	}
	check(whitelistapplication.Revision(nil))
	if err := db.CreateWhitelist("active", ""); err != nil {
		t.Fatal(err)
	}
	if err := db.ReplaceWhitelistEntries([]string{"EXAMPLE.com", "!blocked.com"}, "active"); err != nil {
		t.Fatal(err)
	}
	if err := db.SetWhitelistActive("active", true); err != nil {
		t.Fatal(err)
	}
	check("a0f458ff7ae9f5978d2beb0a07253af0bd3ccd28dbe30e11adc08fc86f6c05e9")
	if err := db.SetWhitelistActive("active", false); err != nil {
		t.Fatal(err)
	}
	check(whitelistapplication.Revision(nil))
	if got := handlerResponse(t, frontend, "SYNC_WHITELISTS\n1\nx\n\n"); !strings.HasPrefix(got, "ERROR\n") {
		t.Fatalf("arguments accepted: %q", got)
	}
	for _, request := range []string{"SYNC_WHITELISTS\n\n", "SYNC_WHITELISTS\n3\neve\n5\ntoken\n\n"} {
		if got := handlerResponse(t, backend, request); !strings.HasPrefix(got, "ERROR\n") {
			t.Fatalf("remote client requested global sync: %q", got)
		}
	}
	failedDB := failedWhitelistReadDB{DBInstance: db}
	failedFrontend := NewFrontendHandler(failedDB, nil, nil, bus)
	if got := handlerResponse(t, failedFrontend, "SYNC_WHITELISTS\n\n"); !strings.HasPrefix(got, "ERROR\n") {
		t.Fatalf("database failure hidden: %q", got)
	}
	failedBackend := NewBackendHandler(failedDB, nil, nil, bus)
	if got := handlerResponse(t, failedBackend, "GET_WHITELIST\n3\neve\n5\ntoken\n\n"); !strings.HasPrefix(got, "ERROR\n") {
		t.Fatalf("failed download returned successful empty whitelist: %q", got)
	}
	bus.Publish(notification.Event{Type: notification.WhitelistChanged})
	for _, expected := range []string{"EVENT\n", "17\n", "WHITELIST_CHANGED\n", "\n"} {
		if got, err := reader.ReadString('\n'); err != nil || got != expected {
			t.Fatalf("invalid/failed request published a sync event: %q, %v", got, err)
		}
	}
}

type failedWhitelistReadDB struct{ database.DBInstance }

func (failedWhitelistReadDB) GetWhitelists() ([]*entity.Whitelist, error) {
	return nil, errors.New("simulated database read failure")
}
