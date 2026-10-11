//go:build windows

package rpc

import (
	"bufio"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/infrastructure/notification"
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

// Exercise the actual client handler: removing SET_FILTRATION from the remote
// command dispatcher must reject a request without mutating server settings.
func TestBackendRejectsRemoteFiltrationChange(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "filtration.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	if err := db.SetSetting("filtration_enabled", "1"); err != nil {
		t.Fatal(err)
	}
	h := NewBackendHandler(db, nil, nil, nil)
	response := handlerResponse(t, h, "SET_FILTRATION\n1\n0\n\n")
	if !strings.HasPrefix(response, "ERROR\n") {
		t.Fatalf("response: %q", response)
	}
	value, err := db.GetSetting("filtration_enabled")
	if err != nil || value != "1" {
		t.Fatalf("setting changed: %q, %v", value, err)
	}
}

func TestFrontendRejectsInvalidFiltrationValue(t *testing.T) {
	// Validation must run before accessing the database or publishing events.
	h := NewFrontendHandler(nil, nil, nil, nil)
	for _, request := range []string{
		"SET_FILTRATION\n\n",
		"SET_FILTRATION\n3\nyes\n\n",
		"SET_FILTRATION\n1\n0\n1\n1\n\n",
	} {
		response := handlerResponse(t, h, request)
		if !strings.HasPrefix(response, "ERROR\n") {
			t.Fatalf("response: %q", response)
		}
	}
}

func handlerResponse(t *testing.T, h Handler, request string) string {
	t.Helper()
	server, client := net.Pipe()
	defer client.Close()
	defer server.Close()
	client.SetDeadline(time.Now().Add(5 * time.Second))
	done := make(chan struct{})
	go func() { defer close(done); h.handle(server) }()
	if _, err := client.Write([]byte(request)); err != nil {
		t.Fatal(err)
	}
	reader := bufio.NewReader(client)
	var response strings.Builder
	for {
		line, err := reader.ReadString('\n')
		if err != nil {
			t.Fatal(err)
		}
		response.WriteString(line)
		if line == "\n" {
			break
		}
	}
	client.Close()
	select {
	case <-done:
	case <-time.After(5 * time.Second):
		t.Fatal("handler did not exit")
	}
	return response.String()
}

func TestFrontendFiltrationPersistence(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "filtration.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	h := NewFrontendHandler(db, nil, nil, notification.NewManager())
	for _, value := range []string{"0", "1"} {
		response := handlerResponse(t, h, "SET_FILTRATION\n1\n"+value+"\n\n")
		if response != "OK\n\n" {
			t.Fatalf("response: %q", response)
		}
		stored, err := db.GetSetting("filtration_enabled")
		if err != nil || stored != value {
			t.Fatalf("stored: %q, %v", stored, err)
		}
	}
	// No event bus: publishing after a failed save would panic.
	h = NewFrontendHandler(failedSettingDB{DBInstance: db}, nil, nil, nil)
	response := handlerResponse(t, h, "SET_FILTRATION\n1\n0\n\n")
	if !strings.HasPrefix(response, "ERROR\n") {
		t.Fatalf("response: %q", response)
	}
}

type failedSettingDB struct{ database.DBInstance }

func (failedSettingDB) SetSetting(key, value string) error {
	return errors.New("simulated database write failure")
}

func TestRotatedTokenRecoveryRequiresRegisteredIdentity(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "rotation.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	if _, err := db.CreateUser("alice", "127.0.0.1"); err != nil {
		t.Fatal(err)
	}
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	pub := hex.EncodeToString(elliptic.Marshal(key.Curve, key.X, key.Y))
	if err := db.SetUserSignPublic("alice", pub); err != nil {
		t.Fatal(err)
	}
	if err := db.SetUserToken("alice", "old"); err != nil {
		t.Fatal(err)
	}
	h := NewBackendHandler(db, nil, nil, nil)
	if err := db.SetUserToken("alice", "replacement"); err != nil {
		t.Fatal(err)
	}
	response := handlerResponse(t, h, "GET_WHITELIST\n5\nalice\n3\nold\n\n")
	if !strings.Contains(response, "invalid token") {
		t.Fatalf("stale token accepted: %q", response)
	}
	for _, registered := range []bool{true, false} {
		signer := key
		if !registered {
			signer, err = ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
			if err != nil {
				t.Fatal(err)
			}
		}
		nonce, err := newChallenge("alice")
		if err != nil {
			t.Fatal(err)
		}
		digest := sha256.Sum256([]byte("alice" + nonce))
		signature, err := ecdsa.SignASN1(rand.Reader, signer, digest[:])
		if err != nil {
			t.Fatal(err)
		}
		sig := hex.EncodeToString(signature)
		request := fmt.Sprintf("GET_TOKEN\n5\nalice\n%d\n%s\n%d\n%s\n\n", len(nonce), nonce, len(sig), sig)
		response := handlerResponse(t, h, request)
		if registered {
			if response != "OK\n11\nreplacement\n\n" {
				t.Fatalf("recovery failed: %q", response)
			}
		} else if !strings.HasPrefix(response, "ERROR\n") {
			t.Fatalf("unregistered identity accepted: %q", response)
		}
	}
}

func TestSubscriptionClosesAfterTokenRotation(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "subscription.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	if _, err := db.CreateUser("alice", "127.0.0.1"); err != nil {
		t.Fatal(err)
	}
	if err := db.SetUserToken("alice", "old"); err != nil {
		t.Fatal(err)
	}
	manager := connection.NewMemoryResidentConnectionManager()
	defer manager.Stop()
	h := NewBackendHandler(db, manager, nil, notification.NewManager())
	server, client := net.Pipe()
	defer server.Close()
	defer client.Close()
	client.SetDeadline(time.Now().Add(5 * time.Second))
	done := make(chan struct{})
	go func() { defer close(done); h.handle(server) }()
	if _, err := client.Write([]byte("SUBSCRIBE\n5\nalice\n3\nold\n\n")); err != nil {
		t.Fatal(err)
	}
	reader := bufio.NewReader(client)
	if line, err := reader.ReadString('\n'); err != nil || line != "OK\n" {
		t.Fatalf("subscribe: %q, %v", line, err)
	}
	if line, err := reader.ReadString('\n'); err != nil || line != "\n" {
		t.Fatalf("terminator: %q, %v", line, err)
	}
	if err := db.SetUserToken("alice", "replacement"); err != nil {
		t.Fatal(err)
	}
	if _, err := client.Write([]byte("K\n")); err != nil {
		t.Fatal(err)
	}
	if _, err := reader.ReadByte(); err != io.EOF {
		t.Fatalf("old subscription not closed: %v", err)
	}
	select {
	case <-done:
	case <-time.After(5 * time.Second):
		t.Fatal("handler did not exit")
	}
}
