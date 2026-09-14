package sqlite

import (
	"path/filepath"
	"testing"
)

func newTestDatabase(t *testing.T) *Database {
	t.Helper()
	db := New(filepath.Join(t.TempDir(), "bb-test.db"))
	if err := db.Connect(); err != nil {
		t.Fatalf("connect: %v", err)
	}
	t.Cleanup(func() { db.Disconnect() })
	return db
}

func TestCreateAndGetUser(t *testing.T) {
	db := newTestDatabase(t)

	id, err := db.CreateUser("alice", "10.0.0.1")
	if err != nil {
		t.Fatalf("CreateUser: %v", err)
	}
	if id == "" {
		t.Fatal("empty user id")
	}

	user, err := db.GetUserByName("alice")
	if err != nil {
		t.Fatalf("GetUserByName: %v", err)
	}
	if user.Name != "alice" || user.Addr != "10.0.0.1" {
		t.Fatalf("unexpected user: %+v", user)
	}
}

func TestChangeUserName(t *testing.T) {
	db := newTestDatabase(t)

	if _, err := db.CreateUser("old", "10.0.0.1"); err != nil {
		t.Fatalf("CreateUser: %v", err)
	}

	if err := db.ChangeUserName("old", "new"); err != nil {
		t.Fatalf("ChangeUserName to free name: %v", err)
	}
	if _, err := db.GetUserByName("old"); err == nil {
		t.Fatal("old name should be gone")
	}
	if _, err := db.GetUserByName("new"); err != nil {
		t.Fatalf("new name should exist: %v", err)
	}

	// Self rename (no-op) must not error.
	if err := db.ChangeUserName("new", "new"); err != nil {
		t.Fatalf("self rename: %v", err)
	}

	// Rename to an existing other user's name must be rejected.
	if _, err := db.CreateUser("bob", "10.0.0.2"); err != nil {
		t.Fatalf("create bob: %v", err)
	}
	if err := db.ChangeUserName("new", "bob"); err == nil {
		t.Fatal("expected unique constraint error renaming to taken name")
	}
}

func TestChangeUserAddrToNewAddress(t *testing.T) {
	db := newTestDatabase(t)

	if _, err := db.CreateUser("carol", "10.0.0.9"); err != nil {
		t.Fatalf("CreateUser: %v", err)
	}

	// Bug #1 regression: changing to a not-yet-known address previously panicked.
	if err := db.ChangeUserAddr("carol", "10.1.1.1"); err != nil {
		t.Fatalf("ChangeUserAddr to new IP: %v", err)
	}

	user, err := db.GetUserByName("carol")
	if err != nil {
		t.Fatalf("GetUserByName: %v", err)
	}
	if user.Addr != "10.1.1.1" {
		t.Fatalf("addr not updated: %q", user.Addr)
	}
}

func TestChangeUsersWhitelistAndDeleteUsers(t *testing.T) {
	db := newTestDatabase(t)

	if _, err := db.CreateUser("dave", "10.0.0.3"); err != nil {
		t.Fatalf("create dave: %v", err)
	}
	if err := db.CreateWhitelist("main", ""); err != nil {
		t.Fatalf("create whitelist: %v", err)
	}
	if err := db.SetSetting("active_whitelist", "main"); err != nil {
		t.Fatalf("set active: %v", err)
	}

	if err := db.ChangeUsersWhitelist("main", "dave"); err != nil {
		t.Fatalf("ChangeUsersWhitelist: %v", err)
	}

	// Missing user in a whitelist-change transaction must fail cleanly (and not
	// leak the transaction, per the NewTransaction fix).
	if err := db.ChangeUsersWhitelist("main", "ghost"); err == nil {
		t.Fatal("expected error for unknown user")
	}

	if err := db.DeleteUsers("dave"); err != nil {
		t.Fatalf("DeleteUsers: %v", err)
	}
	if _, err := db.GetUserByName("dave"); err == nil {
		t.Fatal("dave should be deleted")
	}
}

func TestWhitelistEntries(t *testing.T) {
	db := newTestDatabase(t)

	if err := db.CreateWhitelist("main", ""); err != nil {
		t.Fatalf("create whitelist: %v", err)
	}

	if err := db.ReplaceWhitelistEntries([]string{"google.com", "*.github.com"}, "main"); err != nil {
		t.Fatalf("ReplaceWhitelistEntries: %v", err)
	}

	entries, err := db.GetWhitelistEntries("main")
	if err != nil {
		t.Fatalf("GetWhitelistEntries: %v", err)
	}
	if len(entries) != 2 {
		t.Fatalf("expected 2 entries, got %d", len(entries))
	}

	if err := db.ReplaceWhitelistEntries([]string{}, "main"); err != nil {
		t.Fatalf("replace with empty: %v", err)
	}
	entries, err = db.GetWhitelistEntries("main")
	if err != nil {
		t.Fatalf("GetWhitelistEntries after clear: %v", err)
	}
	if len(entries) != 0 {
		t.Fatalf("expected 0 entries, got %d", len(entries))
	}
}

// TestActiveWhitelistSetting exercises the DB primitives backing the
// client-facing GET_WHITELIST path: a known user + active whitelist setting
// yields the active whitelist's entries.
func TestActiveWhitelistSetting(t *testing.T) {
	db := newTestDatabase(t)

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

	// Mirrors BackendHandler.GetWhitelist (see windows-tagged handler test).
	if _, err := db.GetUserByName("eve"); err != nil {
		t.Fatalf("user lookup: %v", err)
	}
	active, err := db.GetSetting("active_whitelist")
	if err != nil || active != "default" {
		t.Fatalf("active whitelist: %q, %v", active, err)
	}
	entries, err := db.GetWhitelistEntries(active)
	if err != nil || len(entries) != 1 || entries[0].Value != "example.com" {
		t.Fatalf("unexpected served whitelist: %v, %v", entries, err)
	}
}
