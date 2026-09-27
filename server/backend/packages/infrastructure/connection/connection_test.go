package connection

import (
	"sync"
	"testing"
	"time"

	"bigbrother_server_backend/packages/domain/entity"
)

func testUser(name string) *entity.User {
	return &entity.User{Id: "id-" + name, Name: name, Addr: "1.2.3.4"}
}

func newTestManager() *MemoryResidentConnectionManager {
	return NewMemoryResidentConnectionManager()
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
			m.DeleteConnection(name)
		}(i)
	}
	wg.Wait()
}
