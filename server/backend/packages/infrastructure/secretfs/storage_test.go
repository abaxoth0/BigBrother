package secretfs

import (
	"bigbrother_server_backend/packages/infrastructure/tlsutil"
	"database/sql"
	_ "github.com/ncruces/go-sqlite3/driver"
	"os"
	"path/filepath"
	"testing"
)

func TestMigrationPreservesWALDataAndTLSIdentity(t *testing.T) {
	parent := filepath.Join(t.TempDir(), "application")
	if err := os.MkdirAll(parent, 0700); err != nil {
		t.Fatal(err)
	}
	legacy := filepath.Join(parent, "bb-server.db")
	source, err := sql.Open("sqlite3", legacy)
	if err != nil {
		t.Fatal(err)
	}
	defer source.Close()
	for _, statement := range []string{"PRAGMA journal_mode=WAL", "CREATE TABLE credentials(token TEXT)", "INSERT INTO credentials VALUES('existing-token')"} {
		if _, err := source.Exec(statement); err != nil {
			t.Fatal(err)
		}
	}
	certPath, keyPath := filepath.Join(parent, "tls", "server.crt"), filepath.Join(parent, "tls", "server.key")
	cert, err := tlsutil.LoadOrCreateCert(certPath, keyPath)
	if err != nil {
		t.Fatal(err)
	}
	original, _ := tlsutil.Fingerprint(cert)
	target, err := Prepare(legacy)
	if err != nil {
		t.Fatal(err)
	}
	if target != filepath.Join(parent, "data", "bb-server.db") {
		t.Fatal(target)
	}
	migrated, err := sql.Open("sqlite3", target)
	if err != nil {
		t.Fatal(err)
	}
	defer migrated.Close()
	var token string
	if err := migrated.QueryRow("SELECT token FROM credentials").Scan(&token); err != nil || token != "existing-token" {
		t.Fatalf("WAL data: %q %v", token, err)
	}
	loaded, err := tlsutil.LoadOrCreateCert(filepath.Join(parent, "data", "tls", "server.crt"), filepath.Join(parent, "data", "tls", "server.key"))
	if err != nil {
		t.Fatal(err)
	}
	actual, _ := tlsutil.Fingerprint(loaded)
	if actual != original {
		t.Fatal("TLS identity changed")
	}
	if _, err := migrated.Exec("UPDATE credentials SET token='new-token'"); err != nil {
		t.Fatal(err)
	}
	repeated, err := Prepare(legacy)
	if err != nil || repeated != target {
		t.Fatalf("repeat: %s %v", repeated, err)
	}
	if err := migrated.QueryRow("SELECT token FROM credentials").Scan(&token); err != nil || token != "new-token" {
		t.Fatal("migration overwrote active data", err)
	}
	// Retired original TLS files are not the active identity after migration.
	if err := os.Remove(certPath); err != nil {
		t.Fatal(err)
	}
	if _, err := Prepare(legacy); err != nil {
		t.Fatal("retired TLS source affected active storage", err)
	}
	if _, err := os.Stat(legacy); err != nil {
		t.Fatal("original was removed", err)
	}
}

func TestMigrationRejectsIncompleteTLSIdentity(t *testing.T) {
	parent := filepath.Join(t.TempDir(), "application")
	if err := os.MkdirAll(filepath.Join(parent, "tls"), 0700); err != nil {
		t.Fatal(err)
	}
	key := filepath.Join(parent, "tls", "server.key")
	if err := os.WriteFile(key, []byte("original-key"), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := Prepare(filepath.Join(parent, "bb-server.db")); err == nil {
		t.Fatal("incomplete identity accepted")
	}
	contents, err := os.ReadFile(key)
	if err != nil || string(contents) != "original-key" {
		t.Fatal("original key changed")
	}
}
