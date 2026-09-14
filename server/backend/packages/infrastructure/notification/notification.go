package notification

import (
	"bufio"
	"fmt"
	"net"
	"strconv"
	"sync"
)

type EventType int

const (
	UserConnected EventType = iota
	UserDisconnected
	PendingAdded
	PendingRemoved
	UserApproved
	UserRejected
	WhitelistChanged
	FiltrationToggled
)

func (e EventType) String() string {
	switch e {
	case UserConnected:
		return "USER_CONNECTED"
	case UserDisconnected:
		return "USER_DISCONNECTED"
	case PendingAdded:
		return "PENDING_ADDED"
	case PendingRemoved:
		return "PENDING_REMOVED"
	case UserApproved:
		return "USER_APPROVED"
	case UserRejected:
		return "USER_REJECTED"
	case WhitelistChanged:
		return "WHITELIST_CHANGED"
	case FiltrationToggled:
		return "FILTRATION_TOGGLED"
	default:
		return "UNKNOWN"
	}
}

type Event struct {
	Type EventType
	Data map[string]string
}

type Subscriber struct {
	Conn      net.Conn
	Writer    *bufio.Writer
	EventCh   chan Event
	done      chan struct{}
	closeOnce sync.Once
}

func (s *Subscriber) Close() {
	s.closeOnce.Do(func() {
		close(s.done)
	})
}

func (s *Subscriber) IsClosed() bool {
	select {
	case <-s.done:
		return true
	default:
		return false
	}
}

func NewSubscriber(conn net.Conn, bufSize int) *Subscriber {
	return &Subscriber{
		Conn:    conn,
		Writer:  bufio.NewWriterSize(conn, bufSize),
		EventCh: make(chan Event, 64),
		done:    make(chan struct{}),
	}
}

type Manager struct {
	mu          sync.RWMutex
	subscribers map[*Subscriber]struct{}
}

func NewManager() *Manager {
	return &Manager{
		subscribers: make(map[*Subscriber]struct{}),
	}
}

func (m *Manager) Add(sub *Subscriber) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.subscribers[sub] = struct{}{}
	go m.writeLoop(sub)
}

func (m *Manager) Remove(sub *Subscriber) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if _, ok := m.subscribers[sub]; ok {
		delete(m.subscribers, sub)
		sub.Close()
	}
}

func (m *Manager) Publish(event Event) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	for sub := range m.subscribers {
		select {
		case sub.EventCh <- event:
		default:
			// Subscriber too slow — drop event
		}
	}
}

func (m *Manager) writeEvent(sub *Subscriber, event Event) error {
	writer := sub.Writer
	if _, err := fmt.Fprint(writer, "EVENT\n"); err != nil {
		writer.Flush()
		return err
	}
	if err := writeTLV(writer, event.Type.String()); err != nil {
		writer.Flush()
		return err
	}
	for k, v := range event.Data {
		if err := writeTLV(writer, k+"="+v); err != nil {
			writer.Flush()
			return err
		}
	}
	if _, err := fmt.Fprint(writer, "\n"); err != nil {
		writer.Flush()
		return err
	}
	return writer.Flush()
}

func writeTLV(writer *bufio.Writer, value string) error {
	if _, err := fmt.Fprint(writer, strconv.Itoa(len(value))+"\n"); err != nil {
		return err
	}
	if _, err := fmt.Fprint(writer, value+"\n"); err != nil {
		return err
	}
	return nil
}

func (m *Manager) writeLoop(sub *Subscriber) {
	defer func() {
		m.Remove(sub)
	}()
	for {
		select {
		case event := <-sub.EventCh:
			if err := m.writeEvent(sub, event); err != nil {
				// Stalled or broken subscriber: close its conn so the handler's
				// read loop unblocks too, then exit; the deferred Remove cleans up.
				sub.Conn.Close()
				return
			}
		case <-sub.done:
			return
		}
	}
}
