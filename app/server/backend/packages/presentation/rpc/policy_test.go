//go:build windows

package rpc

import (
	"bufio"
	"encoding/json"
	"fmt"
	"net"
	"path/filepath"
	"strings"
	"testing"
	"time"

	whitelistapplication "bigbrother_server_backend/packages/application/whitelist"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/infrastructure/notification"
)

func TestClientPolicySubscriptionAndDetails(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "policy.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	if _, err := db.CreateUser("eve", "[::1]:1984"); err != nil {
		t.Fatal(err)
	}
	if err := db.SetUserToken("eve", "token"); err != nil {
		t.Fatal(err)
	}
	manager := connection.NewMemoryResidentConnectionManager()
	defer manager.Stop()
	changes := make(chan notification.Event, 32)
	bus := notification.NewManagerWithNotifier(func(event notification.Event) {
		if event.Type == notification.ClientPolicyChanged {
			changes <- event
		}
	})
	backend := NewBackendHandler(db, manager, nil, bus)
	if err := backend.ConnectUser("eve", "[::1]:1984"); err != nil {
		t.Fatal(err)
	}
	frontend := NewFrontendHandler(db, manager, nil, bus)
	readDetails := func() map[string]any {
		t.Helper()
		response := handlerResponse(t, frontend, "GET_CLIENTS_DETAILS\n\n")
		lines := strings.Split(response, "\n")
		var clients []map[string]any
		if len(lines) < 3 || json.Unmarshal([]byte(lines[2]), &clients) != nil || len(clients) != 1 {
			t.Fatalf("invalid details response: %q", response)
		}
		return clients[0]
	}
	initial := readDetails()
	if initial["policy"] != nil || initial["whitelistCurrent"] != nil || initial["lastActivity"] == nil || initial["address"] != "[::1]:1984" {
		t.Fatalf("unexpected initial details: %v", initial)
	}
	if response := handlerResponse(t, backend, "SUBSCRIBE\n3\neve\n3\nbad\n\n"); !strings.HasPrefix(response, "ERROR\n") {
		t.Fatalf("unauthenticated subscription accepted: %q", response)
	}
	server, client := net.Pipe()
	defer client.Close()
	done := make(chan struct{})
	go func() { backend.handle(server); close(done) }()
	client.SetDeadline(time.Now().Add(5 * time.Second))
	fmt.Fprint(client, "SUBSCRIBE\n3\neve\n5\ntoken\n\n")
	reader := bufio.NewReader(client)
	if status, _ := reader.ReadString('\n'); status != "OK\n" {
		t.Fatalf("subscribe: %q", status)
	}
	reader.ReadString('\n')
	revision := whitelistapplication.Revision(nil)
	policy := fmt.Sprintf(`POLICY {"whitelistRevision":%q,"filtrationEnabled":true,"blockIpv6":false,"canSetIpv6Block":true}`, revision)
	fmt.Fprintln(client, policy)
	deadline := time.Now().Add(time.Second)
	for manager.GetAllConnections()[0].Policy == nil && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	current := readDetails()
	if current["whitelistCurrent"] != true {
		t.Fatalf("empty whitelist not acknowledged: %v", current)
	}
	select {
	case event := <-changes:
		if event.Data["name"] != "eve" {
			t.Fatalf("policy change target: %v", event)
		}
	case <-time.After(time.Second):
		t.Fatal("policy report did not notify the frontend")
	}
	beforeRepeated := manager.GetAllConnections()[0].LastActivity
	fmt.Fprintln(client, policy)
	deadline = time.Now().Add(time.Second)
	for !manager.GetAllConnections()[0].LastActivity.After(beforeRepeated) && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	select {
	case <-changes:
		t.Fatal("unchanged policy emitted a change event")
	default:
	}
	reported, ok := current["policy"].(map[string]any)
	if !ok || reported["filtrationEnabled"] != true || reported["blockIpv6"] != false || reported["canSetIpv6Block"] != true {
		t.Fatalf("policy lost: %v", current)
	}
	if err := db.CreateWhitelist("active", ""); err != nil {
		t.Fatal(err)
	}
	if err := db.AddWhitelistEntry("example.com", "active"); err != nil {
		t.Fatal(err)
	}
	if err := db.SetWhitelistActive("active", true); err != nil {
		t.Fatal(err)
	}
	if changed := readDetails(); changed["whitelistCurrent"] != false {
		t.Fatalf("stale whitelist reported current: %v", changed)
	}
	if err := db.SetWhitelistActive("active", false); err != nil {
		t.Fatal(err)
	}
	if readDetails()["whitelistCurrent"] != true {
		t.Fatal("empty composition revision changed")
	}
	if err := db.ReplaceWhitelistEntries([]string{"EXAMPLE.com", "!blocked.com"}, "active"); err != nil {
		t.Fatal(err)
	}
	if err := db.SetWhitelistActive("active", true); err != nil {
		t.Fatal(err)
	}
	revision = "a0f458ff7ae9f5978d2beb0a07253af0bd3ccd28dbe30e11adc08fc86f6c05e9"
	policy = fmt.Sprintf(`POLICY {"whitelistRevision":%q,"filtrationEnabled":true,"blockIpv6":false}`, revision)
	fmt.Fprintln(client, policy)
	deadline = time.Now().Add(time.Second)
	for manager.GetAllConnections()[0].Policy.WhitelistRevision != revision && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if readDetails()["whitelistCurrent"] != true {
		t.Fatal("populated firewall revision differs from composed server rules")
	}
	for _, invalid := range []string{
		`POLICY {}`, `POLICY {"whitelistRevision":"bad","filtrationEnabled":false,"blockIpv6":true}`,
		`POLICY {"whitelistRevision":"","filtrationEnabled":"bad","blockIpv6":true}`, `POLICY {`,
	} {
		before := manager.GetAllConnections()[0].LastActivity
		fmt.Fprintln(client, invalid+"\nK")
		deadline = time.Now().Add(time.Second)
		for !manager.GetAllConnections()[0].LastActivity.After(before) && time.Now().Before(deadline) {
			time.Sleep(time.Millisecond)
		}
		if manager.GetAllConnections()[0].Policy.WhitelistRevision != revision {
			t.Fatalf("invalid report replaced policy: %s", invalid)
		}
	}
	fmt.Fprintln(client, `POLICY {"whitelistRevision":"","filtrationEnabled":false,"blockIpv6":true}`)
	deadline = time.Now().Add(time.Second)
	for manager.GetAllConnections()[0].Policy.WhitelistRevision != "" && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if unavailable := readDetails(); unavailable["whitelistCurrent"] != nil {
		t.Fatalf("unavailable firewall reported current: %v", unavailable)
	}
	before := manager.GetAllConnections()[0].LastActivity
	fmt.Fprintln(client, "K")
	deadline = time.Now().Add(time.Second)
	for !manager.GetAllConnections()[0].LastActivity.After(before) && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if !manager.GetAllConnections()[0].LastActivity.After(before) {
		t.Fatal("heartbeat did not advance activity")
	}
	if err := db.SetUserToken("eve", "rotated"); err != nil {
		t.Fatal(err)
	}
	fmt.Fprintln(client, policy)
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("revoked subscription retained policy access")
	}
	if len(manager.GetAllConnections()) != 0 {
		t.Fatal("revoked subscription remained connected")
	}
}

func TestFrontendClientIpv6Control(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "ipv6-control.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	_, err := db.CreateUser("eve", "10.0.0.2")
	if err != nil {
		t.Fatal(err)
	}
	if err := db.SetUserToken("eve", "token"); err != nil {
		t.Fatal(err)
	}
	manager := connection.NewMemoryResidentConnectionManager()
	defer manager.Stop()
	user, err := db.GetUserByName("eve")
	if err != nil {
		t.Fatal(err)
	}
	manager.NewConnection(user)
	var events []notification.Event
	bus := notification.NewManagerWithNotifier(func(event notification.Event) { events = append(events, event) })
	frontend := NewFrontendHandler(db, manager, nil, bus)
	backend := NewBackendHandler(db, manager, nil, bus)
	request := func(name, value string) string {
		return fmt.Sprintf("SET_CLIENT_IPV6_BLOCK\n%d\n%s\n%d\n%s\n\n", len(name), name, len(value), value)
	}
	for _, policy := range []*connection.PolicyStatus{nil, {BlockIpv6: true}} {
		if policy != nil {
			manager.ReportPolicy("eve", *policy)
		}
		if got := handlerResponse(t, frontend, request("eve", "0")); !strings.HasPrefix(got, "ERROR\n") {
			t.Fatalf("unsupported client accepted control: %q", got)
		}
	}
	manager.ReportPolicy("eve", connection.PolicyStatus{CanSetIpv6Block: true, BlockIpv6: true})
	for _, invalid := range []string{
		"SET_CLIENT_IPV6_BLOCK\n\n", "SET_CLIENT_IPV6_BLOCK\n3\neve\n\n",
		request("eve", "true"), request("eve", "01"), request("missing", "0"),
		"SET_CLIENT_IPV6_BLOCK\n3\neve\n1\n0\n1\n1\n\n",
	} {
		if got := handlerResponse(t, frontend, invalid); !strings.HasPrefix(got, "ERROR\n") {
			t.Fatalf("invalid control request: %q", got)
		}
	}
	if len(events) != 0 {
		t.Fatalf("invalid request emitted an event: %v", events)
	}
	for _, enabled := range []string{"0", "1"} {
		if got := handlerResponse(t, frontend, request("eve", enabled)); got != "OK\n\n" {
			t.Fatalf("control request: %q", got)
		}
		event := events[len(events)-1]
		if event.Type != notification.ClientIpv6BlockChanged || event.Type.String() != "CLIENT_IPV6_BLOCK_CHANGED" ||
			event.Data["name"] != "eve" || event.Data["enabled"] != enabled {
			t.Fatalf("incorrect control event: %v", event)
		}
	}
	client, _ := manager.GetConnection("eve")
	if !client.Policy.BlockIpv6 {
		t.Fatal("queued request changed reported policy before acknowledgment")
	}
	for _, raw := range []string{request("eve", "0"), "SET_CLIENT_IPV6_BLOCK\n3\neve\n5\ntoken\n1\n0\n\n"} {
		if got := handlerResponse(t, backend, raw); !strings.HasPrefix(got, "ERROR\n") {
			t.Fatalf("remote client changed policy: %q", got)
		}
	}
	manager.DeleteConnection("eve")
	if got := handlerResponse(t, frontend, request("eve", "0")); !strings.HasPrefix(got, "ERROR\n") {
		t.Fatalf("offline client accepted control: %q", got)
	}
	if len(events) != 2 {
		t.Fatalf("unexpected control events: %v", events)
	}
}
