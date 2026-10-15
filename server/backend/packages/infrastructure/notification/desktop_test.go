package notification

import (
	"errors"
	"testing"
)

func TestDesktopNotificationsWithoutFrontend(t *testing.T) {
	var messages []desktopMessage
	notifier := NewDesktopNotifier(func(title, body string) error {
		messages = append(messages, desktopMessage{title, body})
		return nil
	}, nil)
	bus := NewManagerWithNotifier(notifier.Notify)
	for _, event := range []Event{
		{Type: UserConnected, Data: map[string]string{"name": "Alice", "addr": "192.0.2.1"}},
		{Type: UserDisconnected, Data: map[string]string{"name": "Alice"}},
		{Type: PendingAdded, Data: map[string]string{"name": "Боб"}},
		{Type: WhitelistChanged},
		{Type: PendingRemoved},
	} {
		bus.Publish(event)
	}
	notifier.Close()
	if len(messages) != 3 {
		t.Fatalf("expected 3 notifications with no frontend subscribers, got %v", messages)
	}
	if messages[0].title != "Клиент подключился" || messages[0].body != "Alice (192.0.2.1)" ||
		messages[1].title != "Клиент отключился" || messages[2].body != "Боб" {
		t.Fatalf("unexpected notifications: %v", messages)
	}
	bus.Publish(Event{Type: UserConnected})
	notifier.Close()
}

func TestDesktopDeliveryFailureDoesNotStopEvents(t *testing.T) {
	deliveryError := errors.New("helper unavailable")
	var delivered, failures int
	notifier := NewDesktopNotifier(func(_, _ string) error {
		delivered++
		return deliveryError
	}, func(err error) {
		if err == deliveryError {
			failures++
		}
	})
	notifier.Notify(Event{Type: UserConnected})
	notifier.Notify(Event{Type: UserDisconnected})
	notifier.Close()
	if delivered != 2 || failures != 2 {
		t.Fatalf("expected both failed deliveries to be reported, got %d deliveries and %d errors", delivered, failures)
	}
}

func TestDesktopQueueIsBounded(t *testing.T) {
	started, release := make(chan struct{}), make(chan struct{})
	var delivered int
	notifier := NewDesktopNotifier(func(_, _ string) error {
		delivered++
		if delivered == 1 {
			close(started)
			<-release
		}
		return nil
	}, nil)
	notifier.Notify(Event{Type: UserConnected})
	<-started
	for i := 0; i < 1000; i++ {
		notifier.Notify(Event{Type: UserConnected})
	}
	close(release)
	notifier.Close()
	if delivered != 65 {
		t.Fatalf("expected one active delivery plus 64 queued, got %d", delivered)
	}
}
