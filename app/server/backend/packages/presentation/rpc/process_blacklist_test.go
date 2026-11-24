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
