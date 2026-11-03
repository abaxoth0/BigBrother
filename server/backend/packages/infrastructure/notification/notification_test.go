package notification

import (
	"io"
	"net"
	"testing"
	"time"
)

func TestDisconnectSendsTerminalEventAndCloses(t *testing.T) {
	server, client := net.Pipe()
	defer client.Close()
	client.SetReadDeadline(time.Now().Add(3 * time.Second))
	sub := NewSubscriber(server, 4096)
	m := NewManager()
	if err := m.Add(sub); err != nil {
		t.Fatal(err)
	}
	m.RemoveWithEvent(sub, &Event{Type: ClientDisconnected, Data: map[string]string{"name": "alice"}})
	m.Publish(Event{Type: WhitelistChanged})
	raw, err := io.ReadAll(client)
	if err != nil {
		t.Fatal(err)
	}
	if string(raw) != "EVENT\n19\nCLIENT_DISCONNECTED\n10\nname=alice\n\n" {
		t.Fatalf("unexpected terminal frame: %q", raw)
	}
}

func TestRemovalUnblocksStalledSubscriber(t *testing.T) {
	for _, terminal := range []*Event{nil, {Type: ClientDisconnected}} {
		server, client := net.Pipe()
		m := NewManager()
		sub := NewSubscriber(server, 4096)
		if err := m.Add(sub); err != nil {
			t.Fatal(err)
		}
		m.Publish(Event{Type: WhitelistChanged})
		closed := make(chan error, 1)
		go func() { _, err := server.Read(make([]byte, 1)); closed <- err }()
		m.RemoveWithEvent(sub, terminal)
		select {
		case err := <-closed:
			if err == nil {
				t.Fatal("unexpected data on subscriber")
			}
		case <-time.After(3 * time.Second):
			t.Fatal("subscriber socket stayed open")
		}
		client.Close()
	}
}

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
