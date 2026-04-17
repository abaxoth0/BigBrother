package rpc

import (
	"bigbrother_server_backend/packages/domain/entity"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database"
	"errors"
	"fmt"
	"time"
)

type ServerStatus struct {
	Connections []*connection.Connection
	Uptime	 time.Duration
}

type Server interface {
	GetWhitelist(username string) []*entity.WhitelistEntry
	SetWhitelist(whitelistName string, usernames ...string) error

	AddWhitelistEntry(whitelistName, entry string) error
	DeleteWhitelistEntry(whitelistName, entry string) error

	GetConnections() []*connection.Connection
	RegisterUser(username, addr string) error
	DeleteUsers(username ...string) error

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
	db 			database.DBInstance
	connManager connection.Manager
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

func (s *DuplexServer) RegisterUser(username, addr string) error {
	if _, err := s.db.CreateUser(username, addr); err != nil {
		return err
	}
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
		Uptime: time.Since(startTime),
	}
}
