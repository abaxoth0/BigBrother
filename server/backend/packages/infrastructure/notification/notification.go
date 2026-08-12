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

func (m *Manager) writeEvent(sub *Subscriber, event Event) {
	writer := sub.Writer
	fmt.Fprint(writer, "EVENT\n")
	writeTLV(writer, event.Type.String())
	for k, v := range event.Data {
		writeTLV(writer, k+"="+v)
	}
	fmt.Fprint(writer, "\n")
	writer.Flush()
}

func writeTLV(writer *bufio.Writer, value string) {
	fmt.Fprint(writer, strconv.Itoa(len(value))+"\n")
	fmt.Fprint(writer, value+"\n")
}

func (m *Manager) writeLoop(sub *Subscriber) {
	defer func() {
		m.Remove(sub)
	}()
	for {
		select {
		case event := <-sub.EventCh:
			m.writeEvent(sub, event)
		case <-sub.done:
			return
		}
	}
}
