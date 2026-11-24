package notification

import (
	"context"
	"errors"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestDesktopNotificationsWithoutFrontend(t *testing.T) {
	var messages []desktopMessage
	notifier := NewDesktopNotifier(func(_ context.Context, title, body string) error {
		messages = append(messages, desktopMessage{title: title, body: body})
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
	notifier := NewDesktopNotifier(func(_ context.Context, _, _ string) error {
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

func TestDesktopBurstIsSummarizedWithoutLosingCounts(t *testing.T) {
	started, release := make(chan struct{}), make(chan struct{})
	var messages []desktopMessage
	notifier := NewDesktopNotifier(func(_ context.Context, title, body string) error {
		messages = append(messages, desktopMessage{title: title, body: body})
		if len(messages) == 1 {
			close(started)
			<-release
		}
		return nil
	}, nil, nil)
	notifier.Notify(Event{Type: UserConnected})
	<-started
	for i := 0; i < 1000; i++ {
		notifier.Notify(Event{Type: EventType(i % 3)})
	}
	close(release)
	notifier.Close()
	if len(messages) != 2 || messages[1].title != "События сервера" ||
		!strings.Contains(messages[1].body, "Подключений: 334; отключений: 333; запросов на регистрацию: 333") {
		t.Fatalf("expected an individual notification and a complete burst summary, got %v", messages)
	}
}

func TestDesktopNotificationsCanBeReenabled(t *testing.T) {
	var enabled atomic.Bool
	var delivered int
	notifier := NewDesktopNotifier(func(_ context.Context, _, _ string) error {
		delivered++
		return nil
	}, nil, func() (bool, error) { return enabled.Load(), nil })
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
	notifier := NewDesktopNotifier(func(_ context.Context, _, _ string) error {
		delivered++
		if delivered == 1 {
			close(started)
			<-release
		}
		return nil
	}, nil, func() (bool, error) { return enabled.Load(), nil })
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

func TestDesktopPreferenceFailureUsesLastKnownValue(t *testing.T) {
	var fail atomic.Bool
	var enabled atomic.Bool
	enabled.Store(true)
	var delivered int
	var failures atomic.Int32
	notifier := NewDesktopNotifier(func(_ context.Context, _, _ string) error { delivered++; return nil },
		func(error) { failures.Add(1) }, func() (bool, error) {
			if fail.Load() {
				return false, errors.New("database busy")
			}
			return enabled.Load(), nil
		})
	notifier.Notify(Event{Type: UserConnected})
	fail.Store(true)
	notifier.Notify(Event{Type: UserDisconnected})
	notifier.Close()
	if delivered != 2 || failures.Load() == 0 {
		t.Fatalf("got %d deliveries and %d failures", delivered, failures.Load())
	}

	enabled.Store(false)
	fail.Store(false)
	notifier = NewDesktopNotifier(func(_ context.Context, _, _ string) error { t.Error("disabled notification delivered"); return nil }, nil,
		func() (bool, error) {
			if fail.Load() {
				return true, errors.New("database busy")
			}
			return enabled.Load(), nil
		})
	notifier.Notify(Event{Type: UserConnected})
	fail.Store(true)
	notifier.Notify(Event{Type: UserConnected})
	notifier.Close()
}

func TestDesktopCloseCancelsBlockedDelivery(t *testing.T) {
	started := make(chan struct{})
	notifier := NewDesktopNotifier(func(ctx context.Context, _, _ string) error {
		close(started)
		<-ctx.Done()
		return ctx.Err()
	}, nil, nil)
	notifier.closeTimeout = 20 * time.Millisecond
	notifier.Notify(Event{Type: UserConnected})
	<-started
	notifier.Notify(Event{Type: UserDisconnected})
	done := make(chan struct{})
	go func() { notifier.Close(); close(done) }()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("notification shutdown did not cancel delivery")
	}
	notifier.Close()
}
