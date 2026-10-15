package connection

import (
	"bigbrother_server_backend/packages/domain/entity"
	"sync"
	"time"

	"github.com/abaxoth0/Ain/errs"
	"github.com/google/uuid"
)

const ConnectionTTL = time.Second * 15

var (
	ErrAlreadyConnected   = errs.NewStatusError("User already connected", 409)
	ErrConnectionNotFound = errs.NewStatusError("Connection not found", 404)
)

type Connection struct {
	id         string
	user       *entity.User
	created_at time.Time
	expires_at time.Time
}

func newConnection(user *entity.User) *Connection {
	now := time.Now()
	return &Connection{
		id:         uuid.NewString(),
		user:       user,
		created_at: now,
		expires_at: now.Add(ConnectionTTL),
	}
}

// Returns shallow copy
func (s *Connection) GetUser() entity.User {
	return *s.user
}

// Used for tracking connected users
type Manager interface {
	NewConnection(user *entity.User) (*Connection, error)
	GetConnection(username string) (*Connection, error)
	DeleteConnection(username string) error
	RefreshConnection(username string) error
	GetAllConnections() []*Connection
	CleanupExpired()
}

// Stores connections in application memory
type MemoryResidentConnectionManager struct {
	mu             sync.RWMutex
	connections    map[string]*Connection
	stopCleanup    chan struct{}
	cleanupOnce    sync.Once
	onDisconnected func(entity.User)
}

func NewMemoryResidentConnectionManager() *MemoryResidentConnectionManager {
	return NewMemoryResidentConnectionManagerWithDisconnectHandler(nil)
}

func NewMemoryResidentConnectionManagerWithDisconnectHandler(handler func(entity.User)) *MemoryResidentConnectionManager {
	m := &MemoryResidentConnectionManager{
		connections:    make(map[string]*Connection),
		stopCleanup:    make(chan struct{}),
		onDisconnected: handler,
	}
	go m.cleanupLoop()
	return m
}

func (m *MemoryResidentConnectionManager) cleanupLoop() {
	ticker := time.NewTicker(time.Second * 5)
	defer ticker.Stop()
	for {
		select {
		case <-m.stopCleanup:
			return
		case <-ticker.C:
			m.CleanupExpired()
		}
	}
}

func (m *MemoryResidentConnectionManager) Stop() {
	m.cleanupOnce.Do(func() { close(m.stopCleanup) })
}

func (m *MemoryResidentConnectionManager) CleanupExpired() {
	now := time.Now()
	var expired []entity.User
	m.mu.Lock()
	for name, conn := range m.connections {
		if now.After(conn.expires_at) {
			expired = append(expired, conn.GetUser())
			delete(m.connections, name)
		}
	}
	m.mu.Unlock()
	if m.onDisconnected != nil {
		for _, user := range expired {
			m.onDisconnected(user)
		}
	}
}

func (m *MemoryResidentConnectionManager) NewConnection(user *entity.User) (*Connection, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	if _, ok := m.connections[user.Name]; ok {
		return nil, ErrAlreadyConnected
	}

	conn := newConnection(user)
	m.connections[user.Name] = conn

	return conn, nil
}

func (m *MemoryResidentConnectionManager) GetConnection(username string) (*Connection, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	conn, ok := m.connections[username]
	if !ok {
		return nil, ErrConnectionNotFound
	}
	return conn, nil
}

func (m *MemoryResidentConnectionManager) GetAllConnections() []*Connection {
	m.mu.RLock()
	defer m.mu.RUnlock()

	list := make([]*Connection, 0, len(m.connections))
	for _, conn := range m.connections {
		list = append(list, conn)
	}
	return list
}

func (m *MemoryResidentConnectionManager) DeleteConnection(username string) error {
	m.mu.Lock()
	conn, exists := m.connections[username]
	delete(m.connections, username)
	m.mu.Unlock()
	if exists && m.onDisconnected != nil {
		m.onDisconnected(conn.GetUser())
	}
	return nil
}

func (m *MemoryResidentConnectionManager) RefreshConnection(username string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	conn, ok := m.connections[username]
	if !ok {
		return ErrConnectionNotFound
	}
	conn.expires_at = time.Now().Add(ConnectionTTL)
	return nil
}
