package connection

import (
	"bigbrother_server_backend/packages/domain/entity"
	"time"

	"github.com/abaxoth0/Ain/errs"
	"github.com/google/uuid"
)

const ConnectionTTL = time.Minute * 10

var (
	ErrAlreadyConnected   = errs.NewStatusError("User already connected", 409)
	ErrConnectionNotFound = errs.NewStatusError("Connection not found", 404)
)

type Connection struct {
	id 			string
	user 		*entity.User
	created_at 	time.Time
	expires_at 	time.Time
}

func newConnection(user *entity.User) *Connection {
	now := time.Now()
	return &Connection{
		id: uuid.NewString(),
		user: user,
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
	connections map[string]*Connection
}

func NewMemoryResidentConnectionManager() *MemoryResidentConnectionManager {
	return &MemoryResidentConnectionManager{
		connections: make(map[string]*Connection),
	}
}

func (m *MemoryResidentConnectionManager) CleanupExpired() {
	now := time.Now()
	for name, conn := range m.connections {
		if now.After(conn.expires_at) {
			delete(m.connections, name)
		}
	}
}

func (m *MemoryResidentConnectionManager) NewConnection(user *entity.User) (*Connection, error) {
	m.CleanupExpired()
	if _, ok := m.connections[user.Name]; ok {
		return nil, ErrAlreadyConnected
	}

	conn := newConnection(user)
	m.connections[user.Name] = conn

	return conn, nil
}

func (m *MemoryResidentConnectionManager) GetConnection(username string) (*Connection, error) {
	m.CleanupExpired()
	conn, ok := m.connections[username]
	if !ok {
		return nil, ErrConnectionNotFound
	}
	return conn, nil
}

func (m *MemoryResidentConnectionManager) GetAllConnections() []*Connection {
	m.CleanupExpired()
	list := make([]*Connection, 0, len(m.connections))
	for _, conn := range m.connections {
		list = append(list, conn)
	}
	return list
}

func (m *MemoryResidentConnectionManager) DeleteConnection(username string) error {
	delete(m.connections, username)
	return nil
}

func (m *MemoryResidentConnectionManager) RefreshConnection(username string) error {
	conn, ok := m.connections[username]
	if !ok {
		return ErrConnectionNotFound
	}
	conn.expires_at = time.Now().Add(ConnectionTTL)
	return nil
}
