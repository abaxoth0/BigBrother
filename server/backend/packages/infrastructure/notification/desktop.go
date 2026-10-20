package notification

import (
	"sync"
)

type desktopMessage struct {
	title string
	body  string
}

type DesktopNotifier struct {
	mu      sync.Mutex
	closed  bool
	enabled func() bool
	queue   chan desktopMessage
	done    chan struct{}
}

func NewDesktopNotifier(deliver func(string, string) error, reportError func(error), enabled func() bool) *DesktopNotifier {
	n := &DesktopNotifier{queue: make(chan desktopMessage, 64), done: make(chan struct{}), enabled: enabled}
	go func() {
		defer close(n.done)
		for message := range n.queue {
			if n.enabled != nil && !n.enabled() {
				continue
			}
			if err := deliver(message.title, message.body); err != nil && reportError != nil {
				reportError(err)
			}
		}
	}()
	return n
}

func (n *DesktopNotifier) Notify(event Event) {
	if n.enabled != nil && !n.enabled() {
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
	case n.queue <- desktopMessage{title, body}:
	default:
	}
}

func (n *DesktopNotifier) Close() {
	n.mu.Lock()
	if !n.closed {
		n.closed = true
		close(n.queue)
	}
	n.mu.Unlock()
	<-n.done
}
