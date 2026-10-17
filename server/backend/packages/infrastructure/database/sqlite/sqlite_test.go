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

func TestSetWhitelistsActiveTransaction(t *testing.T) {
	db := newTestDatabase(t)
	for _, name := range []string{"first", "second", "untouched"} {
		if err := db.CreateWhitelist(name, ""); err != nil {
			t.Fatal(err)
		}
	}
	changed, err := db.SetWhitelistsActive(map[string]bool{"first": true, "second": true})
	if err != nil || !changed {
		t.Fatalf("batch save: %v, %v", changed, err)
	}
	changed, err = db.SetWhitelistsActive(map[string]bool{"first": true, "second": true})
	if err != nil || changed {
		t.Fatalf("unchanged save: %v, %v", changed, err)
	}
	if _, err := db.SetWhitelistsActive(map[string]bool{"first": false, "zz-missing": true}); err == nil {
		t.Fatal("batch with missing list succeeded")
	}
	for name, active := range map[string]bool{"first": true, "second": true, "untouched": false} {
		wl, err := db.GetWhitelistByName(name)
		if err != nil || wl.IsActive != active {
			t.Fatalf("partial batch or unrelated change: %+v, %v", wl, err)
		}
	}
	changed, err = db.SetWhitelistsActive(map[string]bool{"first": false, "second": false})
	if err != nil || !changed {
		t.Fatalf("disable batch: %v, %v", changed, err)
	}
	for _, name := range []string{"first", "second"} {
		wl, err := db.GetWhitelistByName(name)
		if err != nil || wl.IsActive {
			t.Fatalf("batch disable not persisted: %+v, %v", wl, err)
		}
	}
}

func TestUserToken(t *testing.T) {
	db := newTestDatabase(t)

	if _, err := db.CreateUser("alice", "10.0.0.1"); err != nil {
		t.Fatalf("CreateUser: %v", err)
	}

	if err := db.SetUserToken("alice", "tok-abc123"); err != nil {
		t.Fatalf("SetUserToken: %v", err)
	}

	u, err := db.GetUserByName("alice")
	if err != nil {
		t.Fatalf("GetUserByName: %v", err)
	}
	if u.Token != "tok-abc123" {
		t.Fatalf("unexpected token: %q", u.Token)
	}

	// Clearing the token is allowed (revoke path).
	if err := db.SetUserToken("alice", ""); err != nil {
		t.Fatalf("SetUserToken(empty): %v", err)
	}
	u, err = db.GetUserByName("alice")
	if err != nil {
		t.Fatalf("GetUserByName: %v", err)
	}
	if u.Token != "" {
		t.Fatalf("expected revoked token, got %q", u.Token)
	}
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
