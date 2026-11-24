//go:build windows

package rpc

import (
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/infrastructure/notification"
	"fmt"
	"path/filepath"
	"strings"
	"testing"
)

func TestPriorityWhitelistProtocol(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "priorities.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	bus := notification.NewManager()
	frontend := NewFrontendHandler(db, nil, nil, bus)
	backend := NewBackendHandler(db, nil, nil, bus)
	request := func(command string, args ...string) string {
		var text strings.Builder
		text.WriteString(command + "\n")
		for _, arg := range args {
			fmt.Fprintf(&text, "%d\n%s\n", len(arg), arg)
		}
		text.WriteByte('\n')
		return text.String()
	}
	ok := func(command string, args ...string) {
		t.Helper()
		if got := handlerResponse(t, frontend, request(command, args...)); got != "OK\n\n" {
			t.Fatalf("%s: %q", command, got)
		}
	}
	ok("CREATE_WHITELIST", "base")
	ok("SAVE_WHITELIST", "base", "!github.com:443")
	ok("SET_WHITELIST_ACTIVE", "base", "1")
	ok("CREATE_TEMPORARY_WHITELIST", "temporary", "900", "0")
	source := []string{"# grant", "[10]", "github.com:443", "[20]!gist.github.com"}
	ok("SAVE_WHITELIST", append([]string{"temporary"}, source...)...)
	ok("SET_WHITELIST_ACTIVE", "temporary", "1")
	if got := handlerResponse(t, frontend, request("GET_WHITELISTS")); !strings.Contains(got, "temporary:2:true") {
		t.Fatalf("declaration counted as a rule: %q", got)
	}
	var saved strings.Builder
	saved.WriteString("OK\n")
	for _, line := range source {
		fmt.Fprintf(&saved, "%d\n%s\n", len(line), line)
	}
	saved.WriteByte('\n')
	if got := handlerResponse(t, frontend, request("GET_WHITELIST", "temporary")); got != saved.String() {
		t.Fatalf("source or order changed: %q", got)
	}
	for _, invalid := range [][]string{{"github.com", "[10]"}, {"[10]", "[20]"}, {"[-1]github.com"}} {
		if got := handlerResponse(t, frontend, request("SAVE_WHITELIST", append([]string{"temporary"}, invalid...)...)); !strings.HasPrefix(got, "ERROR\n") {
			t.Fatalf("accepted malformed header: %v: %q", invalid, got)
		}
	}
	if got := handlerResponse(t, frontend, request("GET_WHITELIST", "temporary")); got != saved.String() {
		t.Fatal("rejected save changed the installed list")
	}
	response := handlerResponse(t, frontend, request("GET_COMPOSED_WHITELIST_DETAILS"))
	for _, rule := range []string{"!github.com:443", "[10]github.com:443", "[20]!gist.github.com"} {
		if !strings.Contains(response, rule) {
			t.Fatalf("missing effective priority: %q", response)
		}
	}
	response = handlerResponse(t, frontend, request("GET_WHITELIST_CONFLICTS"))
	if !strings.Contains(response, `"allowedPriority":10`) || !strings.Contains(response, `"pattern":"github.com:443"`) {
		t.Fatalf("priority conflict metadata: %q", response)
	}
	if _, err := db.CreateUser("eve", "10.0.0.2"); err != nil {
		t.Fatal(err)
	}
	if err := db.SetUserToken("eve", "token"); err != nil {
		t.Fatal(err)
	}
	response = handlerResponse(t, backend, request("GET_WHITELIST", "eve", "token"))
	if !strings.Contains(response, "[10]github.com:443") || strings.Contains(response, "\n4\n[10]\n") {
		t.Fatalf("client download did not resolve list defaults: %q", response)
	}
	ok("UPDATE_WHITELIST_TIMER", "temporary", "900", "0", "0")
	response = handlerResponse(t, backend, request("GET_WHITELIST", "eve", "token"))
	if response != "OK\n15\n!github.com:443\n\n" {
		t.Fatalf("expired priority grant retained: %q", response)
	}
}
