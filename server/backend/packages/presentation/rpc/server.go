package rpc

import (
	"bigbrother_server_backend/packages/domain/entity"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database"
	"errors"
	"fmt"
	"sync"
	"time"
)

const PendingUserTimeout = time.Minute * 10

type ServerStatus struct {
	Connections []*connection.Connection
	Uptime      time.Duration
}

type Server interface {
	GetWhitelist(username string) []*entity.WhitelistEntry
	SetWhitelist(whitelistName string, usernames ...string) error

	AddWhitelistEntry(whitelistName, entry string) error
	DeleteWhitelistEntry(whitelistName, entry string) error

	GetConnections() []*connection.Connection
	RegisterPendingUser(username, addr string) error
	DeleteUsers(username ...string) error

	ConnectUser(name string, addr string) error
	DisconnectUser(name string) error
	RefreshConnection(name string) error
	ChangeUserName(oldName, newName string) error

	Pair(username string) (*connection.Connection, error)
	Forget(username string) error

	GetStatus() *ServerStatus
}

type whitelistDomains map[string]bool

func newWhitelistDomains(domains ...string) whitelistDomains {
	newWl := make(whitelistDomains, len(domains))
	for _, domain := range domains {
		newWl[domain] = true
	}
	return newWl
}

type DuplexServer struct {
	db           database.DBInstance
	connManager  connection.Manager
	pendingUsers map[string]*entity.PendingUser
	pendingMu    sync.Mutex
}

func NewDuplexServer(db database.DBInstance, connManager connection.Manager) *DuplexServer {
	return &DuplexServer{
		db:           db,
		connManager:  connManager,
		pendingUsers: make(map[string]*entity.PendingUser),
	}
}

func (s *DuplexServer) GetWhitelist(username string) []*entity.WhitelistEntry {
	entries, err := s.db.GetUserWhitelistEntries(username)
	if err != nil {
		return nil
	}
	return entries
}

func (s *DuplexServer) SetWhitelist(whitelistName string, usernames ...string) error {
	return s.db.ChangeUsersWhitelist(whitelistName, usernames...)
}

func (s *DuplexServer) AddWhitelistEntry(whitelistName, entry string) error {
	return s.db.AddWhitelistEntry(entry, whitelistName)
}

func (s *DuplexServer) DeleteWhitelistEntry(whitelistName, entry string) error {
	return s.db.DeleteWhitelistEntry(entry, whitelistName)
}

func (s *DuplexServer) GetConnections() []*connection.Connection {
	return s.connManager.GetAllConnections()
}

func (s *DuplexServer) RegisterPendingUser(username, addr string) error {
	s.pendingMu.Lock()
	defer s.pendingMu.Unlock()

	// Auto-expire old pending users
	now := time.Now()
	for name, pu := range s.pendingUsers {
		if now.Sub(pu.CreatedAt) > PendingUserTimeout {
			delete(s.pendingUsers, name)
		}
	}

	// Check if user already exists in DB
	if _, err := s.db.GetUserByName(username); err == nil {
		return errors.New("user already exists")
	}

	// Check if user already pending
	if _, ok := s.pendingUsers[username]; ok {
		return errors.New("user already pending")
	}

	// Add to pending
	s.pendingUsers[username] = &entity.PendingUser{
		Name:      username,
		Addr:      addr,
		CreatedAt: now,
	}

	return nil
}

func (s *DuplexServer) ConnectUser(name string, addr string) error {
	// Get user from DB
	user, err := s.db.GetUserByName(name)
	if err != nil {
		return errors.New("user not found")
	}

	// Check if already connected
	if _, err := s.connManager.GetConnection(name); err == nil {
		return errors.New("user already connected")
	}

	// Update address in DB (DHCP handling)
	if err := s.db.ChangeUserAddr(name, addr); err != nil {
		return err
	}

	// Create connection
	_, err = s.connManager.NewConnection(user)
	return err
}

func (s *DuplexServer) DisconnectUser(name string) error {
	return s.connManager.DeleteConnection(name)
}

func (s *DuplexServer) RefreshConnection(name string) error {
	return s.connManager.RefreshConnection(name)
}

func (s *DuplexServer) ChangeUserName(oldName, newName string) error {
	// Check user is connected
	if _, err := s.connManager.GetConnection(oldName); err != nil {
		return errors.New("user not connected")
	}

	s.pendingMu.Lock()
	defer s.pendingMu.Unlock()

	// Check new name not in DB
	if _, err := s.db.GetUserByName(newName); err == nil {
		return errors.New("name already in use")
	}

	// Check new name not in pending
	if _, ok := s.pendingUsers[newName]; ok {
		return errors.New("name already pending")
	}

	// Update in DB
	if err := s.db.ChangeUserName(oldName, newName); err != nil {
		return err
	}

	// Update connection
	conn, err := s.connManager.GetConnection(oldName)
	if err != nil {
		return nil // DB updated, that's enough
	}

	// Delete old connection, create new with updated name
	s.connManager.DeleteConnection(oldName)
	user := conn.GetUser()
	user.Name = newName
	s.connManager.NewConnection(&user)

	return nil
}

func (s *DuplexServer) DeleteUsers(usernames ...string) error {
	if err := s.db.DeleteUsers(usernames...); err != nil {
		return err
	}
	errMsgs := make([]string, 0, len(usernames))
	for _, username := range usernames {
		if err := s.connManager.DeleteConnection(username); err != nil {
			errMsgs = append(errMsgs, err.Error())
		}
	}
	if len(errMsgs) != 0 {
		msg := fmt.Sprintf("Failed to delete %d connection(-s):\n", len(errMsgs))
		for i, errMsg := range errMsgs {
			msg += fmt.Sprintf("%d %s\n", i+1, errMsg)
		}
		return errors.New(msg)
	}
	return nil
}

func (s *DuplexServer) Pair(username string) (*connection.Connection, error) {
	user, err := s.db.GetUserByName(username)
	if err != nil {
		return nil, err
	}
	return s.connManager.NewConnection(user)
}

func (s *DuplexServer) Forget(username string) error {
	return s.connManager.DeleteConnection(username)
}

var startTime = time.Now()

func (s *DuplexServer) GetStatus() *ServerStatus {
	return &ServerStatus{
		Connections: s.connManager.GetAllConnections(),
		Uptime:      time.Since(startTime),
	}
}
