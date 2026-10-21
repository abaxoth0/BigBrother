package notification

import (
	"context"
	"fmt"
	"sync"
	"time"
)

type desktopMessage struct {
	kind  EventType
	title string
	body  string
}

type DesktopNotifier struct {
	mu           sync.Mutex
	closed       bool
	enabled      func() (bool, error)
	reportError  func(error)
	settingMu    sync.Mutex
	lastEnabled  bool
	overflow     [3]uint64
	ctx          context.Context
	cancel       context.CancelFunc
	closeTimeout time.Duration
	queue        chan desktopMessage
	done         chan struct{}
}

func NewDesktopNotifier(deliver func(context.Context, string, string) error, reportError func(error), enabled func() (bool, error)) *DesktopNotifier {
	ctx, cancel := context.WithCancel(context.Background())
	n := &DesktopNotifier{queue: make(chan desktopMessage, 64), done: make(chan struct{}), enabled: enabled,
		reportError: reportError, lastEnabled: true, ctx: ctx, cancel: cancel, closeTimeout: 2 * time.Second}
	go func() {
		defer close(n.done)
		for message := range n.queue {
			message = n.batch(message)
			if ctx.Err() != nil {
				return
			}
			if !n.isEnabled() {
				continue
			}
			if err := deliver(ctx, message.title, message.body); err != nil && ctx.Err() == nil {
				n.report(err)
			}
		}
	}()
	return n
}

func (n *DesktopNotifier) report(err error) {
	if n.reportError != nil {
		n.reportError(err)
	}
}

func (n *DesktopNotifier) isEnabled() bool {
	if n.ctx.Err() != nil {
		return false
	}
	if n.enabled == nil {
		return true
	}
	n.settingMu.Lock()
	enabled, err := n.enabled()
	if err == nil {
		n.lastEnabled = enabled
	}
	cached := n.lastEnabled
	n.settingMu.Unlock()
	if err != nil {
		n.report(fmt.Errorf("read Windows notification preference (using last known value %t): %w", cached, err))
	}
	return cached
}

func (n *DesktopNotifier) batch(message desktopMessage) desktopMessage {
	n.mu.Lock()
	defer n.mu.Unlock()
	counts := n.overflow
	if len(n.queue) < 4 && counts == [3]uint64{} {
		return message
	}
	n.overflow = [3]uint64{}
	counts[message.kind]++
drain:
	for {
		select {
		case next, ok := <-n.queue:
			if !ok {
				break drain
			}
			counts[next.kind]++
		default:
			break drain
		}
	}
	return desktopMessage{title: "События сервера", body: fmt.Sprintf("Подключений: %d; отключений: %d; запросов на регистрацию: %d", counts[UserConnected], counts[UserDisconnected], counts[PendingAdded])}
}

func (n *DesktopNotifier) Notify(event Event) {
	if !n.isEnabled() {
		return
	}
	var title string
	switch event.Type {
	case UserConnected:
		title = "Клиент подключился"
	case UserDisconnected:
		title = "Клиент отключился"
	case PendingAdded:
		title = "Запрос на регистрацию"
	default:
		return
	}
	body := event.Data["name"]
	if body == "" {
		body = "Клиент"
	}
	if address := event.Data["addr"]; address != "" {
		body += " (" + address + ")"
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.closed {
		return
	}
	select {
	case n.queue <- desktopMessage{kind: event.Type, title: title, body: body}:
	default:
		n.overflow[event.Type]++
	}
}

func (n *DesktopNotifier) Close() {
	n.mu.Lock()
	if !n.closed {
		n.closed = true
		close(n.queue)
	}
	n.mu.Unlock()
	timer := time.NewTimer(n.closeTimeout)
	defer timer.Stop()
	select {
	case <-n.done:
	case <-timer.C:
		n.report(fmt.Errorf("notification shutdown deadline exceeded; cancelling remaining deliveries"))
	}
	n.cancel()
	<-n.done
}
