package notification

import (
	"errors"
	"sync/atomic"
	"testing"
)

func TestDesktopNotificationsWithoutFrontend(t *testing.T) {
	var messages []desktopMessage
	notifier := NewDesktopNotifier(func(title, body string) error {
		messages = append(messages, desktopMessage{title, body})
		return nil
	}, nil, nil)
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
	}, nil)
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
	}, nil, nil)
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

func TestDesktopNotificationsCanBeReenabled(t *testing.T) {
	var enabled atomic.Bool
	var delivered int
	notifier := NewDesktopNotifier(func(_, _ string) error {
		delivered++
		return nil
	}, nil, enabled.Load)
	bus := NewManagerWithNotifier(notifier.Notify)
	for _, kind := range []EventType{UserConnected, UserDisconnected, PendingAdded} {
		bus.Publish(Event{Type: kind})
	}
	enabled.Store(true)
	bus.Publish(Event{Type: UserConnected})
	notifier.Close()
	if delivered != 1 {
		t.Fatalf("expected only the event after re-enabling, got %d", delivered)
	}
}

func TestDisablingDesktopNotificationsDropsQueuedMessages(t *testing.T) {
	var enabled atomic.Bool
	enabled.Store(true)
	started, release := make(chan struct{}), make(chan struct{})
	var delivered int
	notifier := NewDesktopNotifier(func(_, _ string) error {
		delivered++
		if delivered == 1 {
			close(started)
			<-release
		}
		return nil
	}, nil, enabled.Load)
	notifier.Notify(Event{Type: UserConnected})
	<-started
	notifier.Notify(Event{Type: UserDisconnected})
	notifier.Notify(Event{Type: PendingAdded})
	enabled.Store(false)
	close(release)
	notifier.Close()
	if delivered != 1 {
		t.Fatalf("expected queued notifications to be dropped, got %d deliveries", delivered)
	}
}
