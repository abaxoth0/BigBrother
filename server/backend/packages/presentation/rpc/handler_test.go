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

// TestBackendHandlerGetWhitelistServing checks that known users receive the active composition.
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
	if err := db.SetWhitelistActive("default", true); err != nil {
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

	if err := db.CreateWhitelist("second", ""); err != nil {
		t.Fatal(err)
	}
	if err := db.ReplaceWhitelistEntries([]string{"EXAMPLE.com", "!example.com", "second.org"}, "second"); err != nil {
		t.Fatal(err)
	}
	if err := db.SetWhitelistActive("second", true); err != nil {
		t.Fatal(err)
	}
	entries = handler.GetWhitelist("eve")
	if len(entries) != 1 || entries[0].Value != "second.org" {
		t.Fatalf("unexpected composed whitelist: %+v", entries)
	}
	if err := db.SetUserToken("eve", "token"); err != nil {
		t.Fatal(err)
	}
	response := handlerResponse(t, handler, "GET_WHITELIST\n3\neve\n5\ntoken\n\n")
	if response != "OK\n10\nsecond.org\n\n" {
		t.Fatalf("unexpected whitelist response: %q", response)
	}

	frontend := NewFrontendHandler(db, nil, nil, nil)
	if response := handlerResponse(t, frontend, "GET_COMPOSED_WHITELIST\n\n"); response != "OK\n10\nsecond.org\n\n" {
		t.Fatalf("domain checker composition differs from client whitelist: %q", response)
	}
	if err := db.SetWhitelistActive("default", false); err != nil {
		t.Fatal(err)
	}
	if err := db.SetWhitelistActive("second", false); err != nil {
		t.Fatal(err)
	}
	if response := handlerResponse(t, frontend, "GET_COMPOSED_WHITELIST\n\n"); response != "OK\n\n" {
		t.Fatalf("unexpected empty composition: %q", response)
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

func TestFrontendReportsActiveCertificateFingerprint(t *testing.T) {
	fingerprint := strings.Repeat("A", 64)
	h := NewFrontendHandler(nil, nil, nil, nil, fingerprint)
	response := handlerResponse(t, h, "GET_CERTIFICATE_FINGERPRINT\n\n")
	if response != "OK\n64\n"+fingerprint+"\n\n" {
		t.Fatalf("fingerprint response: %q", response)
	}
	response = handlerResponse(t, NewFrontendHandler(nil, nil, nil, nil), "GET_CERTIFICATE_FINGERPRINT\n\n")
	if !strings.HasPrefix(response, "ERROR\n") {
		t.Fatalf("missing fingerprint accepted: %q", response)
	}
}

type failingPortDB struct{ database.DBInstance }

func (failingPortDB) SetSetting(string, string) error { return errors.New("write failed") }

func TestFrontendValidatesAndPersistsServerPort(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "port.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	if err := db.SetSetting("server_port", "1984"); err != nil {
		t.Fatal(err)
	}
	h := NewFrontendHandler(db, nil, nil, nil)
	for _, value := range []string{"0", "65536", "1984junk", "-1", ""} {
		response := handlerResponse(t, h, fmt.Sprintf("SET_SERVER_PORT\n%d\n%s\n\n", len(value), value))
		if !strings.HasPrefix(response, "ERROR\n") {
			t.Fatalf("accepted %q: %q", value, response)
		}
		if saved, _ := db.GetSetting("server_port"); saved != "1984" {
			t.Fatalf("invalid value persisted: %q", saved)
		}
	}
	if response := handlerResponse(t, h, "SET_SERVER_PORT\n4\n4443\n\n"); response != "OK\n\n" {
		t.Fatalf("valid port: %q", response)
	}
	if saved, _ := db.GetSetting("server_port"); saved != "4443" {
		t.Fatalf("port not saved: %q", saved)
	}
	h = NewFrontendHandler(failingPortDB{}, nil, nil, nil)
	if response := handlerResponse(t, h, "SET_SERVER_PORT\n4\n4443\n\n"); !strings.HasPrefix(response, "ERROR\n") {
		t.Fatalf("write failure hidden: %q", response)
	}
}

func TestFrontendWhitelistActivation(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "activation.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	for _, name := range []string{"first", "second"} {
		if err := db.CreateWhitelist(name, ""); err != nil {
			t.Fatal(err)
		}
	}
	if err := db.ReplaceWhitelistEntries([]string{"example.org"}, "first"); err != nil {
		t.Fatal(err)
	}
	if err := db.ReplaceWhitelistEntries([]string{"!example.org"}, "second"); err != nil {
		t.Fatal(err)
	}
	events := 0
	bus := notification.NewManagerWithNotifier(func(event notification.Event) {
		if event.Type == notification.WhitelistChanged {
			events++
		}
	})
	h := NewFrontendHandler(db, nil, nil, bus)
	for _, name := range []string{"first", "second"} {
		request := fmt.Sprintf("SET_WHITELIST_ACTIVE\n%d\n%s\n1\n1\n\n", len(name), name)
		if response := handlerResponse(t, h, request); response != "OK\n\n" {
			t.Fatalf("response: %q", response)
		}
	}
	if events != 2 {
		t.Fatalf("expected two client refresh events, got %d", events)
	}
	response := handlerResponse(t, h, "GET_WHITELISTS\n\n")
	for _, value := range []string{"first:1:true", "second:1:true"} {
		if !strings.Contains(response, value) {
			t.Fatalf("missing active flag in response: %q", response)
		}
	}
	response = handlerResponse(t, h, "GET_WHITELIST_CONFLICTS\n\n")
	if !strings.Contains(response, `[{"pattern":"example.org","allowedBy":["first"],"excludedBy":["second"]}]`) {
		t.Fatalf("conflict sources missing: %q", response)
	}
	for _, request := range []string{
		"SET_WHITELIST_ACTIVE\n\n", "SET_WHITELIST_ACTIVE\n5\nfirst\n\n",
		"SET_WHITELIST_ACTIVE\n5\nfirst\n3\nyes\n\n", "SET_WHITELIST_ACTIVE\n7\nmissing\n1\n1\n\n",
	} {
		if response := handlerResponse(t, h, request); !strings.HasPrefix(response, "ERROR\n") {
			t.Fatalf("invalid request accepted: %q", response)
		}
	}
	h = NewFrontendHandler(failedWhitelistDB{DBInstance: db}, nil, nil, nil)
	if response := handlerResponse(t, h, "SET_WHITELIST_ACTIVE\n5\nfirst\n1\n0\n\n"); !strings.HasPrefix(response, "ERROR\n") {
		t.Fatalf("failed write accepted: %q", response)
	}
	wl, err := db.GetWhitelistByName("first")
	if err != nil || !wl.IsActive || events != 2 {
		t.Fatalf("failed request changed state: %+v, %v, events=%d", wl, err, events)
	}
}

type failedWhitelistDB struct{ database.DBInstance }

func (failedWhitelistDB) SetWhitelistActive(name string, active bool) error {
	return errors.New("simulated database write failure")
}
