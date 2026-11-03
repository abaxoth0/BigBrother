package tlsutil

import (
	"crypto/tls"
	"path/filepath"
	"testing"
)

func TestLoadOrCreateCert(t *testing.T) {
	dir := t.TempDir()
	certPath := filepath.Join(dir, "server.crt")
	keyPath := filepath.Join(dir, "server.key")

	cert, err := LoadOrCreateCert(certPath, keyPath)
	if err != nil {
		t.Fatalf("LoadOrCreateCert: %v", err)
	}
	if len(cert.Certificate) == 0 {
		t.Fatal("no certificate returned")
	}

	cfg, err := ServerConfig(cert)
	if err != nil {
		t.Fatalf("ServerConfig: %v", err)
	}
	if len(cfg.Certificates) != 1 {
		t.Fatalf("expected 1 certificate in config, got %d", len(cfg.Certificates))
	}

	// Reloading from disk must yield an equivalent certificate (stable identity).
	cert2, err := LoadOrCreateCert(certPath, keyPath)
	if err != nil {
		t.Fatalf("reload: %v", err)
	}
	if string(cert.Certificate[0]) != string(cert2.Certificate[0]) {
		t.Fatal("certificate not stable across reloads")
	}
}

func TestServerConfigRejectsEmpty(t *testing.T) {
	if _, err := ServerConfig(tls.Certificate{}); err == nil {
		t.Fatal("expected error for certificate with no cert data")
	}
}
