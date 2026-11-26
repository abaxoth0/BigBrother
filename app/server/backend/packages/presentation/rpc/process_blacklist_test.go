//go:build windows

package rpc

import (
	"path/filepath"
	"strings"
	"testing"

	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/infrastructure/notification"
)

func TestProcessBlacklistPublication(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "process.db"))
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
	events := 0
	bus := notification.NewManagerWithNotifier(func(event notification.Event) {
		if event.Type.String() == "PROCESS_BLACKLIST_CHANGED" {
			events++
		}
	})
	frontend := NewFrontendHandler(db, nil, nil, bus)
	backend := NewBackendHandler(db, nil, nil, nil)
	get := "GET_PROCESS_BLACKLIST\n3\neve\n5\ntoken\n\n"
	source := "3\n[3]\n9\nGame*.EXE\n"
	save := "SAVE_PROCESS_BLACKLIST\n" + source + "\n"
	if got := handlerResponse(t, backend, get); got != "OK\n\n" {
		t.Fatal(got)
	}
	if got := handlerResponse(t, frontend, save); got != "OK\n\n" || events != 1 {
		t.Fatal(got, events)
	}
	if got := handlerResponse(t, frontend, "SAVE_PROCESS_BLACKLIST\n5\n*.exe\n\n"); !strings.HasPrefix(got, "ERROR\n") || events != 1 {
		t.Fatal(got, events)
	}
	if err := db.Disconnect(); err != nil {
		t.Fatal(err)
	}
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	if got := handlerResponse(t, frontend, "GET_PROCESS_BLACKLIST\n\n"); got != "OK\n"+source+"\n" {
		t.Fatal(got)
	}
	if got := handlerResponse(t, backend, get); got != "OK\n12\n[3]game*.exe\n\n" {
		t.Fatal(got)
	}
	for _, request := range []string{save, strings.Replace(get, "token", "wrong", 1)} {
		if got := handlerResponse(t, backend, request); !strings.HasPrefix(got, "ERROR\n") {
			t.Fatal(got)
		}
	}
	if got := handlerResponse(t, frontend, "SAVE_PROCESS_BLACKLIST\n\n"); got != "OK\n\n" {
		t.Fatal(got)
	}
	if got := handlerResponse(t, backend, get); got != "OK\n\n" || events != 2 {
		t.Fatal(got, events)
	}
}

func TestProcessBlacklistSwitch(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "switch.db"))
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

	events := 0
	bus := notification.NewManagerWithNotifier(func(event notification.Event) {
		if event.Type == notification.ProcessBlacklistChanged {
			events++
		}
	})
	frontend := NewFrontendHandler(db, nil, nil, bus)
	backend := NewBackendHandler(db, nil, nil, nil)
	check := func(h Handler, request, want string) {
		t.Helper()
		if got := handlerResponse(t, h, request); got != want {
			t.Fatalf("%q: got %q, want %q", request, got, want)
		}
	}
	reject := func(h Handler, request string) {
		t.Helper()
		if got := handlerResponse(t, h, request); !strings.HasPrefix(got, "ERROR\n") {
			t.Fatalf("%q: expected error, got %q", request, got)
		}
	}
	const get = "GET_PROCESS_BLACKLIST_ENABLED\n\n"
	const set = "SET_PROCESS_BLACKLIST_ENABLED\n"
	const off = set + "1\n0\n\n"
	const on = set + "1\n1\n\n"
	const clientRules = "GET_PROCESS_BLACKLIST\n3\neve\n5\ntoken\n\n"
	const editorRules = "GET_PROCESS_BLACKLIST\n\n"
	const save = "SAVE_PROCESS_BLACKLIST\n"

	check(frontend, get, "OK\n1\n1\n\n")
	check(frontend, save+"8\nGame.exe\n\n", "OK\n\n")
	check(backend, clientRules, "OK\n8\ngame.exe\n\n")
	check(frontend, off, "OK\n\n")
	if events != 2 {
		t.Fatalf("saving and disabling should both notify clients, got %d events", events)
	}
	if err := db.Disconnect(); err != nil {
		t.Fatal(err)
	}
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	check(frontend, get, "OK\n1\n0\n\n")
	check(backend, clientRules, "OK\n\n")
	check(frontend, editorRules, "OK\n8\nGame.exe\n\n")
	reject(backend, strings.Replace(clientRules, "token", "wrong", 1))
	reject(backend, on)
	reject(frontend, "GET_PROCESS_BLACKLIST_ENABLED\n1\n0\n\n")
	for _, args := range []string{"\n", "1\n2\n\n", "1\n1\n1\n0\n\n"} {
		reject(frontend, set+args)
	}
	failing := NewFrontendHandler(failedSettingDB{DBInstance: db}, nil, nil, bus)
	reject(failing, on)
	check(frontend, get, "OK\n1\n0\n\n")
	if events != 2 {
		t.Fatalf("failed requests published events, got %d", events)
	}

	check(frontend, save+"9\nother.exe\n\n", "OK\n\n")
	check(frontend, editorRules, "OK\n9\nother.exe\n\n")
	check(backend, clientRules, "OK\n\n")
	check(frontend, on, "OK\n\n")
	check(frontend, get, "OK\n1\n1\n\n")
	check(backend, clientRules, "OK\n9\nother.exe\n\n")
	if events != 4 {
		t.Fatalf("editing and re-enabling should both notify clients, got %d events", events)
	}
}
