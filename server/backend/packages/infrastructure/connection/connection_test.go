package connection

import (
	"context"
	"errors"
	"sync"
	"testing"
	"time"

	"bigbrother_server_backend/packages/domain/entity"
)

func TestSessionCancellationAndReconnect(t *testing.T) {
	m := newTestManager()
	defer m.Stop()
	old, _ := m.NewConnection(testUser("alice"))
	bob, _ := m.NewConnection(testUser("bob"))
	m.DisconnectConnection("alice")
	if !errors.Is(context.Cause(old.Context()), ErrDisconnectedByServer) || bob.Context().Err() != nil {
		t.Fatal("administrative disconnect did not cancel only the selected session")
	}
	current, _ := m.NewConnection(testUser("alice"))
	m.DeleteSession("alice", old.ID())
	if _, err := m.RefreshSession("alice", old.ID(), &PolicyStatus{BlockIpv6: true}); err != ErrConnectionNotFound {
		t.Fatalf("old session refreshed a replacement: %v", err)
	}
	got, err := m.GetConnection("alice")
	if err != nil || got.ID() != current.ID() || got.Policy != nil || current.Context().Err() != nil {
		t.Fatal("old subscription cleanup damaged the new session")
	}
	m.DeleteSession("alice", current.ID())
	if !errors.Is(context.Cause(current.Context()), context.Canceled) {
		t.Fatal("normal disconnect did not cancel its session")
	}
	m.mu.Lock()
	m.connections["bob"].expires_at = time.Now().Add(-time.Second)
	m.mu.Unlock()
	m.CleanupExpired()
	if !errors.Is(context.Cause(bob.Context()), context.DeadlineExceeded) {
		t.Fatal("expired session was not canceled")
	}
	last, _ := m.NewConnection(testUser("alice"))
	m.Stop()
	if last.Context().Err() == nil {
		t.Fatal("shutdown left a session active")
	}
}

func testUser(name string) *entity.User {
	return &entity.User{Id: "id-" + name, Name: name, Addr: "1.2.3.4"}
}

func newTestManager() *MemoryResidentConnectionManager {
	return NewMemoryResidentConnectionManager()
}

func TestDisconnectHandler(t *testing.T) {
	var names []string
	var m *MemoryResidentConnectionManager
	m = NewMemoryResidentConnectionManagerWithDisconnectHandler(func(user entity.User) {
		m.GetAllConnections()
		names = append(names, user.Name)
	})
	defer m.Stop()
	m.NewConnection(testUser("alice"))
	m.DeleteConnection("alice")
	m.DeleteConnection("alice")
	m.NewConnection(testUser("bob"))
	m.mu.Lock()
	m.connections["bob"].expires_at = time.Now().Add(-time.Second)
	m.mu.Unlock()
	m.CleanupExpired()
	m.CleanupExpired()
	if len(names) != 2 || names[0] != "alice" || names[1] != "bob" {
		t.Fatalf("expected one callback per removed connection, got %v", names)
	}
}

func TestManagerLifecycle(t *testing.T) {
	m := newTestManager()
	defer m.Stop()

	conn, err := m.NewConnection(testUser("alice"))
	if err != nil {
		t.Fatalf("NewConnection: %v", err)
	}
	if conn == nil {
		t.Fatal("NewConnection returned nil")
	}

	if _, err := m.NewConnection(testUser("alice")); err != ErrAlreadyConnected {
		t.Fatalf("expected ErrAlreadyConnected, got %v", err)
	}

	got, err := m.GetConnection("alice")
	if err != nil || got == nil {
		t.Fatalf("GetConnection: %v", err)
	}

	if err := m.RefreshConnection("alice"); err != nil {
		t.Fatalf("RefreshConnection: %v", err)
	}

	if err := m.DeleteConnection("alice"); err != nil {
		t.Fatalf("DeleteConnection: %v", err)
	}
	if _, err := m.GetConnection("alice"); err != ErrConnectionNotFound {
		t.Fatalf("expected ErrConnectionNotFound, got %v", err)
	}
}

func TestManagerCleanupExpired(t *testing.T) {
	m := newTestManager()
	defer m.Stop()

	if _, err := m.NewConnection(testUser("bob")); err != nil {
		t.Fatalf("NewConnection: %v", err)
	}

	m.mu.Lock()
	m.connections["bob"].expires_at = time.Now().Add(-time.Second)
	m.mu.Unlock()

	m.CleanupExpired()

	if _, err := m.GetConnection("bob"); err != ErrConnectionNotFound {
		t.Fatalf("expected expired connection removed, got %v", err)
	}
}

func TestManagerConcurrentAccess(t *testing.T) {
	m := newTestManager()
	defer m.Stop()

	var wg sync.WaitGroup
	for i := 0; i < 32; i++ {
		wg.Add(1)
		go func(n int) {
			defer wg.Done()
			name := string(rune('a' + n%4))
			// Mix of reads/writes/cleanup across goroutines.
			m.NewConnection(testUser(name))
			m.GetConnection(name)
			m.GetAllConnections()
			m.RefreshConnection(name)
			m.ReportPolicy(name, PolicyStatus{FiltrationEnabled: true})
			m.DeleteConnection(name)
		}(i)
	}
	wg.Wait()
}

func TestPolicyAndActivitySnapshots(t *testing.T) {
	m := newTestManager()
	defer m.Stop()
	initial, _ := m.NewConnection(testUser("alice"))
	if initial.LastActivity.IsZero() || initial.Policy != nil {
		t.Fatalf("unexpected initial status: %+v", initial)
	}
	policy := PolicyStatus{WhitelistRevision: "revision", FiltrationEnabled: true, BlockIpv6: false}
	if err := m.ReportPolicy("alice", policy); err != nil {
		t.Fatal(err)
	}
	reported := m.GetAllConnections()[0]
	if reported.Policy == nil || *reported.Policy != policy || reported.LastActivity.Before(initial.LastActivity) {
		t.Fatalf("missing report: %+v", reported)
	}
	if initial.Policy != nil {
		t.Fatal("report modified an earlier snapshot")
	}
	m.RefreshConnection("alice")
	refreshed, _ := m.GetConnection("alice")
	if !refreshed.LastActivity.After(reported.LastActivity) || *refreshed.Policy != policy {
		t.Fatal("legacy heartbeat did not update activity or lost policy")
	}
	m.DeleteConnection("alice")
	if err := m.ReportPolicy("alice", policy); err != ErrConnectionNotFound {
		t.Fatalf("report created an unauthenticated connection: %v", err)
	}
	m.NewConnection(testUser("alice"))
	if m.GetAllConnections()[0].Policy != nil {
		t.Fatal("reconnect retained stale policy")
	}
}
