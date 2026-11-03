//go:build windows

package rpc

import (
	"bigbrother_server_backend/packages/domain/entity"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/infrastructure/notification"
	"encoding/json"
	"fmt"
	"path/filepath"
	"strings"
	"testing"
)

func TestFrontendTemporaryWhitelists(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "temporary.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	events := 0
	bus := notification.NewManagerWithNotifier(func(event notification.Event) {
		if event.Type == notification.WhitelistChanged {
			events++
		}
	})
	h := NewFrontendHandler(db, nil, nil, bus)
	backend := NewBackendHandler(db, nil, nil, bus)
	request := func(command string, args ...string) string {
		var text strings.Builder
		text.WriteString(command + "\n")
		for _, arg := range args {
			fmt.Fprintf(&text, "%d\n%s\n", len(arg), arg)
		}
		text.WriteString("\n")
		return text.String()
	}
	ok := func(command string, args ...string) {
		t.Helper()
		if got := handlerResponse(t, h, request(command, args...)); got != "OK\n\n" {
			t.Fatalf("%s: %q", command, got)
		}
	}
	ok("CREATE_TEMPORARY_WHITELIST", "Временный:доступ", "900", "0")
	ok("SET_WHITELIST_ACTIVE", "Временный:доступ", "1")
	ok("UPDATE_WHITELIST_TIMER", "Временный:доступ", "600", "1200", "1")
	response := handlerResponse(t, h, request("GET_WHITELISTS_DETAILS"))
	fields := strings.Split(response, "\n")
	var rows []*entity.Whitelist
	if len(fields) < 3 || fields[0] != "OK" {
		t.Fatalf("details: %q", response)
	}
	if err := json.Unmarshal([]byte(fields[2]), &rows); err != nil {
		t.Fatal(err)
	}
	if len(rows) != 1 || rows[0].Name != "Временный:доступ" || !rows[0].IsActive || rows[0].Temporary == nil || rows[0].Temporary.DurationSeconds != 600 || rows[0].Temporary.RemainingSeconds < 1190 || !rows[0].Temporary.AutoDelete {
		t.Fatalf("timer metadata: %s", fields[2])
	}
	ok("RESET_WHITELIST_TIMERS", "Временный:доступ")
	row, err := db.GetWhitelistByName("Временный:доступ")
	if err != nil || !row.IsActive || row.Temporary.RemainingSeconds > 600 || row.Temporary.RemainingSeconds < 590 {
		t.Fatalf("reset: %+v %v", row, err)
	}
	savedEvents := events
	for _, args := range [][]string{
		{"UPDATE_WHITELIST_TIMER", "Временный:доступ", "-1", "unchanged", "0"},
		{"UPDATE_WHITELIST_TIMER", "Временный:доступ", "900", "-1", "0"},
		{"UPDATE_WHITELIST_TIMER", "Временный:доступ", "900", "unchanged", "yes"},
		{"UPDATE_WHITELIST_TIMER", "Временный:доступ", "900junk", "unchanged", "0"},
		{"UPDATE_WHITELIST_TIMER", "Временный:доступ", "900"},
		{"CREATE_TEMPORARY_WHITELIST", "invalid", "0", "0"},
		{"RESET_WHITELIST_TIMERS", "missing"},
		{"SET_SAVE_TEMPORARY_WHITELIST_STATE", "yes"},
	} {
		if got := handlerResponse(t, h, request(args[0], args[1:]...)); !strings.HasPrefix(got, "ERROR\n") {
			t.Fatalf("invalid command accepted: %v -> %q", args, got)
		}
	}
	if events != savedEvents {
		t.Fatal("failed command published policy changes")
	}
	ok("UPDATE_WHITELIST_TIMER", "Временный:доступ", "600", "0", "1")
	if events != savedEvents+1 {
		t.Fatal("immediate expiry did not publish client refresh")
	}
	if _, err := db.GetWhitelistByName("Временный:доступ"); err == nil {
		t.Fatal("immediate expiry did not auto-delete")
	}
	for _, enabled := range []string{"1", "0"} {
		ok("SET_SAVE_TEMPORARY_WHITELIST_STATE", enabled)
		if got := handlerResponse(t, h, request("GET_SAVE_TEMPORARY_WHITELIST_STATE")); got != fmt.Sprintf("OK\n1\n%s\n\n", enabled) {
			t.Fatalf("persistence setting: %q", got)
		}
	}
	for _, command := range []string{"CREATE_TEMPORARY_WHITELIST", "UPDATE_WHITELIST_TIMER", "RESET_WHITELIST_TIMERS", "SET_SAVE_TEMPORARY_WHITELIST_STATE"} {
		if got := handlerResponse(t, backend, request(command)); !strings.HasPrefix(got, "ERROR\n") {
			t.Fatalf("remote timer administration accepted: %s", command)
		}
	}
}
