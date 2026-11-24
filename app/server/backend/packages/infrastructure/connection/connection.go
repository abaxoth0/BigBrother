package connection

import (
	"bigbrother_server_backend/packages/domain/entity"
	"context"
	"sync"
	"time"

	"github.com/abaxoth0/Ain/errs"
	"github.com/google/uuid"
)

const ConnectionTTL = time.Second * 15

var (
	ErrAlreadyConnected     = errs.NewStatusError("User already connected", 409)
	ErrConnectionNotFound   = errs.NewStatusError("Connection not found", 404)
	ErrDisconnectedByServer = errs.NewStatusError("Disconnected by server", 409)
)

type Connection struct {
	id           string
	user         *entity.User
	created_at   time.Time
	expires_at   time.Time
	LastActivity time.Time
	Policy       *PolicyStatus
	ctx          context.Context
	cancel       context.CancelCauseFunc
}

func (s *Connection) ID() string               { return s.id }
func (s *Connection) Context() context.Context { return s.ctx }

type PolicyStatus struct {
	WhitelistRevision string `json:"whitelistRevision"`
	FiltrationEnabled bool   `json:"filtrationEnabled"`
	BlockIpv6         bool   `json:"blockIpv6"`
	CanSetIpv6Block   bool   `json:"canSetIpv6Block"`
}

func newConnection(user *entity.User) *Connection {
	now := time.Now()
	ctx, cancel := context.WithCancelCause(context.Background())
	return &Connection{
		id:           uuid.NewString(),
		user:         user,
		created_at:   now,
		expires_at:   now.Add(ConnectionTTL),
		LastActivity: now,
		ctx:          ctx,
		cancel:       cancel,
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
	DeleteSession(username, id string) error
	DisconnectConnection(username string) error
	RefreshSession(username, id string, policy *PolicyStatus) (bool, error)
	RefreshConnection(username string) error
	ReportPolicy(username string, policy PolicyStatus) error
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
	m.cleanupOnce.Do(func() {
		close(m.stopCleanup)
		m.mu.Lock()
		defer m.mu.Unlock()
		for _, conn := range m.connections {
			conn.cancel(context.Canceled)
		}
	})
}

func (m *MemoryResidentConnectionManager) CleanupExpired() {
	now := time.Now()
	var expired []entity.User
	m.mu.Lock()
	for name, conn := range m.connections {
		if now.After(conn.expires_at) {
			conn.cancel(context.DeadlineExceeded)
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

	snapshot := *conn
	return &snapshot, nil
}

func (m *MemoryResidentConnectionManager) GetConnection(username string) (*Connection, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	conn, ok := m.connections[username]
	if !ok {
		return nil, ErrConnectionNotFound
	}
	snapshot := *conn
	return &snapshot, nil
}

func (m *MemoryResidentConnectionManager) GetAllConnections() []*Connection {
	m.mu.RLock()
	defer m.mu.RUnlock()

	list := make([]*Connection, 0, len(m.connections))
	for _, conn := range m.connections {
		snapshot := *conn
		list = append(list, &snapshot)
	}
	return list
}

func (m *MemoryResidentConnectionManager) DeleteConnection(username string) error {
	return m.removeConnection(username, "", context.Canceled)
}

func (m *MemoryResidentConnectionManager) DeleteSession(username, id string) error {
	return m.removeConnection(username, id, context.Canceled)
}

func (m *MemoryResidentConnectionManager) DisconnectConnection(username string) error {
	return m.removeConnection(username, "", ErrDisconnectedByServer)
}

func (m *MemoryResidentConnectionManager) removeConnection(username, id string, cause error) error {
	m.mu.Lock()
	conn, exists := m.connections[username]
	if !exists || (id != "" && conn.id != id) {
		m.mu.Unlock()
		return nil
	}
	conn.cancel(cause)
	delete(m.connections, username)
	m.mu.Unlock()
	if exists && m.onDisconnected != nil {
		m.onDisconnected(conn.GetUser())
	}
	return nil
}

func (m *MemoryResidentConnectionManager) RefreshConnection(username string) error {
	_, err := m.RefreshSession(username, "", nil)
	return err
}

func (m *MemoryResidentConnectionManager) ReportPolicy(username string, policy PolicyStatus) error {
	_, err := m.RefreshSession(username, "", &policy)
	return err
}

func (m *MemoryResidentConnectionManager) RefreshSession(username, id string, policy *PolicyStatus) (bool, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	conn, ok := m.connections[username]
	if !ok || (id != "" && conn.id != id) || conn.ctx.Err() != nil {
		return false, ErrConnectionNotFound
	}
	changed := false
	if policy != nil {
		changed = conn.Policy == nil || *conn.Policy != *policy
		snapshot := *policy
		conn.Policy = &snapshot
	}
	conn.LastActivity = time.Now()
	conn.expires_at = conn.LastActivity.Add(ConnectionTTL)
	return changed, nil
}
