package notification

import (
	"net"
	"testing"
	"time"
)

func TestPublishDelivers(t *testing.T) {
	server, client := net.Pipe()
	defer server.Close()
	defer client.Close()

	sub := NewSubscriber(server, 4096)
	m := NewManager()
	m.Add(sub)
	defer m.Remove(sub)

	got := make(chan string, 1)
	go func() {
		buf := make([]byte, 4096)
		n, _ := client.Read(buf)
		got <- string(buf[:n])
	}()

	m.Publish(Event{Type: UserConnected, Data: map[string]string{"name": "alice"}})

	select {
	case raw := <-got:
		if len(raw) < len("EVENT\n") {
			t.Fatalf("short event frame: %q", raw)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("timed out waiting for event frame")
	}
}

func TestWriteLoopRemovesBrokenSubscriber(t *testing.T) {
	server, client := net.Pipe()

	sub := NewSubscriber(server, 4096)
	m := NewManager()
	m.Add(sub)

	// Kill the read side so the next flush fails; the write loop must detect
	// the failure, close the server conn and remove the subscriber.
	client.Close()

	m.Publish(Event{Type: UserConnected, Data: map[string]string{"name": "alice"}})

	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		m.mu.RLock()
		_, present := m.subscribers[sub]
		m.mu.RUnlock()
		if !present {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatal("broken subscriber was never removed")
}
