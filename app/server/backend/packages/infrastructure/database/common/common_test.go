package dbcommon

import (
	"database/sql"
	"errors"
	"path/filepath"
	"testing"
	"time"

	_ "github.com/ncruces/go-sqlite3/driver"
)

// newTestDB opens an isolated sqlite database for the test.
func newTestDB(t *testing.T) *sql.DB {
	t.Helper()
	db, err := sql.Open("sqlite3", filepath.Join(t.TempDir(), "test.db"))
	if err != nil {
		t.Fatalf("open db: %v", err)
	}
	if _, err := db.Exec(`
		CREATE TABLE IF NOT EXISTS items (
			id INTEGER PRIMARY KEY,
			value TEXT NOT NULL
		)`); err != nil {
		t.Fatalf("create table: %v", err)
	}
	t.Cleanup(func() { db.Close() })
	return db
}

func TestNewTransactionRollsBackOnError(t *testing.T) {
	db := newTestDB(t)
	db.SetMaxOpenConns(1)

	if _, err := db.Exec("INSERT INTO items (id, value) VALUES (1, 'seed')"); err != nil {
		t.Fatalf("seed: %v", err)
	}

	// prepFunc writes then fails — the transaction MUST be rolled back so the
	// single pooled connection is released and the database isn't left locked.
	_, err := NewTransaction("failing",
		db,
		func(tx *sql.Tx, id int) error {
			if _, err := tx.Exec("DELETE FROM items WHERE id = ?", id); err != nil {
				return err
			}
			return errors.New("boom")
		},
		1,
	)
	if err == nil {
		t.Fatal("expected NewTransaction to fail")
	}

	// A subsequent write must not block: with the fix the rollback released the
	// connection; without it this would hang until the timeout below.
	done := make(chan error, 1)
	go func() {
		_, err := db.Exec("INSERT INTO items (id, value) VALUES (2, 'after')")
		done <- err
	}()

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("subsequent write failed: %v", err)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("subsequent write blocked: transaction leaked, connection not released")
	}
}

func TestNewTransactionCommit(t *testing.T) {
	db := newTestDB(t)

	tx, err := NewTransaction("ok",
		db,
		func(tx *sql.Tx, value string) error {
			_, err := tx.Exec("INSERT INTO items (value) VALUES (?)", value)
			return err
		},
		"a", "b",
	)
	if err != nil {
		t.Fatalf("NewTransaction: %v", err)
	}
	if err := tx.Commit(); err != nil {
		t.Fatalf("commit: %v", err)
	}

	var count int
	if err := db.QueryRow("SELECT COUNT(*) FROM items").Scan(&count); err != nil {
		t.Fatalf("count: %v", err)
	}
	if count != 2 {
		t.Fatalf("expected 2 rows after commit, got %d", count)
	}
}
