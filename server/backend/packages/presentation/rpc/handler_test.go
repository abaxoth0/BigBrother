//go:build windows

package rpc

import (
	"path/filepath"
	"testing"

	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
)

// TestBackendHandlerGetWhitelistServing is a Windows-only guard for the
// client-facing GET_WHITELIST path: the backend handler must return the global
// active whitelist for a known user and nothing for an unknown one.
func TestBackendHandlerGetWhitelistServing(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "bb-handler-test.db"))
	if err := db.Connect(); err != nil {
		t.Fatalf("connect: %v", err)
	}
	defer db.Disconnect()

	if _, err := db.CreateUser("eve", "10.0.0.4"); err != nil {
		t.Fatalf("create user: %v", err)
	}
	if err := db.CreateWhitelist("default", ""); err != nil {
		t.Fatalf("create whitelist: %v", err)
	}
	if err := db.SetSetting("active_whitelist", "default"); err != nil {
		t.Fatalf("set active: %v", err)
	}
	if err := db.AddWhitelistEntry("example.com", "default"); err != nil {
		t.Fatalf("add entry: %v", err)
	}

	handler := NewBackendHandler(db, nil, nil, nil)
	entries := handler.GetWhitelist("eve")
	if len(entries) != 1 || entries[0].Value != "example.com" {
		t.Fatalf("unexpected served whitelist: %+v", entries)
	}

	if entries := handler.GetWhitelist("ghost"); entries != nil {
		t.Fatalf("expected nil for unknown user, got %+v", entries)
	}
}
