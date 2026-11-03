package sqlite

import (
	whitelistapplication "bigbrother_server_backend/packages/application/whitelist"
	"bigbrother_server_backend/packages/domain/entity"
	"database/sql"
	"path/filepath"
	"reflect"
	"testing"
)

func TestWhitelistComposition(t *testing.T) {
	for _, tc := range []struct {
		name      string
		first     []string
		second    []string
		entries   []string
		conflicts []string
	}{
		{"empty", nil, nil, []string{}, nil},
		{"union", []string{"google.com"}, []string{"github.com"}, []string{"github.com", "google.com"}, nil},
		{"duplicates", []string{" GOOGLE.com ", "google.com", "!EXAMPLE.org"}, []string{"Google.com", "!example.org"}, []string{"!example.org", "google.com"}, nil},
		{"conflicts", []string{"google.com", "*.example.org", "\"github\"", "8.8.8.8"}, []string{"!GOOGLE.COM", "!*.example.org", "!\"github\"", "!8.8.8.8", "google.com"}, []string{"!\"github\"", "!*.example.org", "!8.8.8.8", "!google.com", "\"github\"", "*.example.org", "8.8.8.8", "google.com"}, []string{"\"github\"", "*.example.org", "8.8.8.8", "google.com"}},
		{"internal conflict", []string{"google.com", "!google.com"}, []string{"GOOGLE.COM", "example.org"}, []string{"!google.com", "example.org", "google.com"}, []string{"google.com"}},
		{"overlap", []string{"*.google.com", "\"github\""}, []string{"!mail.google.com", "!api.github.com"}, []string{"!api.github.com", "!mail.google.com", "\"github\"", "*.google.com"}, nil},
		{"comments", []string{"# comment", "; comment", " "}, []string{"example.org"}, []string{"example.org"}, nil},
	} {
		t.Run(tc.name, func(t *testing.T) {
			db := newTestDatabase(t)
			for i, entries := range [][]string{tc.first, tc.second, {"!example.org", "inactive.org"}} {
				name := []string{"first", "second", "inactive"}[i]
				if err := db.CreateWhitelist(name, ""); err != nil {
					t.Fatal(err)
				}
				if err := db.ReplaceWhitelistEntries(entries, name); err != nil {
					t.Fatal(err)
				}
				if i < 2 {
					if err := db.SetWhitelistActive(name, true); err != nil {
						t.Fatal(err)
					}
				}
			}
			result, err := whitelistapplication.Compose(db)
			if err != nil {
				t.Fatal(err)
			}
			values := []string{}
			for _, entry := range result.Entries {
				values = append(values, entry.Value)
			}
			if !reflect.DeepEqual(values, tc.entries) {
				t.Fatalf("entries: %v, want %v", values, tc.entries)
			}
			var conflicts []string
			for _, conflict := range result.Conflicts {
				conflicts = append(conflicts, conflict.Pattern)
				if len(conflict.AllowedBy) == 0 || len(conflict.ExcludedBy) == 0 {
					t.Fatalf("missing sources: %+v", conflict)
				}
			}
			if !reflect.DeepEqual(conflicts, tc.conflicts) {
				t.Fatalf("conflicts: %v, want %v", conflicts, tc.conflicts)
			}
		})
	}
}

func TestWhitelistCompositionLifecycle(t *testing.T) {
	db := newTestDatabase(t)
	for _, name := range []string{"base", "child", "exception"} {
		parentID := ""
		if name == "child" {
			parentID, _ = db.GetWhitelistID("base")
		}
		if err := db.CreateWhitelist(name, parentID); err != nil {
			t.Fatal(err)
		}
		if err := db.SetWhitelistActive(name, true); err != nil {
			t.Fatal(err)
		}
	}
	for name, entries := range map[string][]string{
		"base": {"example.org"}, "child": {"example.org", "child.org"}, "exception": {"!example.org"},
	} {
		if err := db.ReplaceWhitelistEntries(entries, name); err != nil {
			t.Fatal(err)
		}
	}
	result, err := whitelistapplication.Compose(db)
	want := []entity.WhitelistConflict{{Pattern: "example.org", AllowedBy: []string{"base", "child"}, ExcludedBy: []string{"exception"}}}
	if err != nil || !reflect.DeepEqual(result.Conflicts, want) {
		t.Fatalf("composition: %+v, %v", result, err)
	}
	if err := db.SetWhitelistActive("base", false); err != nil {
		t.Fatal(err)
	}
	if err := db.ChangeWhitelistName("child", "renamed"); err != nil {
		t.Fatal(err)
	}
	wl, err := db.GetWhitelistByName("renamed")
	if err != nil || !wl.IsActive {
		t.Fatalf("rename lost active state: %+v, %v", wl, err)
	}
	result, err = whitelistapplication.Compose(db)
	if err != nil || !reflect.DeepEqual(result.Conflicts[0].AllowedBy, []string{"renamed"}) {
		t.Fatalf("inherited conflict: %+v, %v", result, err)
	}
	if err := db.DeleteWhitelist("exception"); err != nil {
		t.Fatal(err)
	}
	result, err = whitelistapplication.Compose(db)
	if err != nil || len(result.Conflicts) != 0 || len(result.Entries) != 2 {
		t.Fatalf("delete did not resolve conflict: %+v, %v", result, err)
	}
	if err := db.SetWhitelistActive("renamed", false); err != nil {
		t.Fatal(err)
	}
	result, err = whitelistapplication.Compose(db)
	if err != nil || len(result.Entries) != 0 || len(result.Conflicts) != 0 {
		t.Fatalf("all inactive: %+v, %v", result, err)
	}
	if err := db.SetWhitelistActive("missing", true); err == nil {
		t.Fatal("activated missing list")
	}
}

func TestMigrateActiveWhitelist(t *testing.T) {
	path := filepath.Join(t.TempDir(), "legacy.db")
	conn, err := sql.Open("sqlite3", path)
	if err != nil {
		t.Fatal(err)
	}
	for _, query := range []string{
		"CREATE TABLE whitelist (id BLOB PRIMARY KEY, name TEXT UNIQUE NOT NULL, parent_id BLOB)",
		"CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
		"INSERT INTO whitelist VALUES ('a', 'legacy', NULL), ('b', 'other', NULL)",
		"INSERT INTO settings VALUES ('active_whitelist', 'legacy')",
	} {
		if _, err := conn.Exec(query); err != nil {
			t.Fatal(err)
		}
	}
	conn.Close()
	db := New(path)
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	wl, err := db.GetWhitelistByName("legacy")
	if err != nil || !wl.IsActive {
		t.Fatalf("legacy selection lost: %+v, %v", wl, err)
	}
	if err := db.SetWhitelistActive("legacy", false); err != nil {
		t.Fatal(err)
	}
	if err := db.SetWhitelistActive("other", true); err != nil {
		t.Fatal(err)
	}
	if err := db.Disconnect(); err != nil {
		t.Fatal(err)
	}
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	for name, active := range map[string]bool{"legacy": false, "other": true} {
		wl, err := db.GetWhitelistByName(name)
		if err != nil || wl.IsActive != active {
			t.Fatalf("restart changed active state: %+v, %v", wl, err)
		}
	}
}
