package pending

import (
	"testing"
	"time"
)

func TestPendingAddPop(t *testing.T) {
	s := NewUserStorage()

	if err := s.Add("alice", "1.2.3.4"); err != nil {
		t.Fatalf("Add: %v", err)
	}
	if err := s.Add("alice", "1.2.3.4"); err == nil {
		t.Fatal("expected duplicate add to fail")
	}
	if err := s.Add("bob", "5.6.7.8"); err != nil {
		t.Fatalf("Add bob: %v", err)
	}

	got := s.GetAll()
	if len(got) != 2 {
		t.Fatalf("expected 2 pending, got %d", len(got))
	}

	user, err := s.Pop("alice")
	if err != nil {
		t.Fatalf("Pop: %v", err)
	}
	if user.Name != "alice" || user.Addr != "1.2.3.4" {
		t.Fatalf("unexpected user: %+v", user)
	}
	if _, err := s.Pop("alice"); err == nil {
		t.Fatal("expected second pop to fail")
	}
}

func TestPendingExpiry(t *testing.T) {
	s := NewUserStorage()

	if err := s.Add("old", "1.1.1.1"); err != nil {
		t.Fatalf("Add: %v", err)
	}

	s.mu.Lock()
	s.users["old"].CreatedAt = time.Now().Add(-PendingUserTimeout - time.Second)
	s.mu.Unlock()

	if got := s.GetAll(); len(got) != 0 {
		t.Fatalf("expected expired pending to be dropped, got %d", len(got))
	}
}
