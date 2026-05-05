//go:build windows

package rpc

import (
	"bigbrother_server_backend/packages/domain/entity"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database"
	"bigbrother_server_backend/packages/infrastructure/pending"
	"bufio"
	"errors"
	"fmt"
	"net"
	"strings"
	"time"
)

// TODO remove?
const (
	StatusOK = 1 + iota
	StatusClients
	StatusServerStatus
)

type BackendHandler struct {
	db           database.DBInstance
	connManager  connection.Manager
	activeWl 	 string // TODO refactor?
	pendingUsers *pending.UserStorage
}

func NewBackendHandler(
	db database.DBInstance,
	connManager connection.Manager,
	pendingUsers *pending.UserStorage,
) *BackendHandler {
	return &BackendHandler{
		db: db,
		connManager: connManager,
		pendingUsers: pendingUsers,
	}
}

func (h *BackendHandler) handle(conn net.Conn) {
	defer conn.Close()
	log.Info("Client connected", nil)

	scanner := bufio.NewScanner(conn)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}

		log.Debug("Received: "+line, nil)

		msg := strings.Split(line, ":")
		if len(msg) > 2 {
			writeError(conn, requestError, "Invalid request syntax (':' duplication)")
			return
		}
		cmd, arg := msg[0], ""
		if len(msg) > 1 {
			arg = strings.TrimSpace(msg[1])
		}

		switch cmd {
		case "GET_WHITELIST":
			wl := h.GetWhitelist(arg)

			writeln(conn, "WHITELIST")
			for _, entry := range wl {
				writeln(conn, entry)
			}

		case "REGISTER":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing name")
				return
			}
			if err := h.RegisterPendingUser(arg, conn.RemoteAddr().String()); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "%d", StatusOK)

		case "CONNECT":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing name")
				return
			}
			if err := h.ConnectUser(arg, conn.RemoteAddr().String()); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "%d", StatusOK)

		case "DISCONNECT":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing name")
				return
			}
			if err := h.DisconnectUser(arg); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "%d", StatusOK)

		case "REFRESH":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing name")
				return
			}
			if err := h.RefreshConnection(arg); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "%d", StatusOK)


		case "GET_STATUS":
			status := h.GetStatus()
			writeln(conn, StatusServerStatus)
			writeln(conn, "uptime:"+status.Uptime.String())
			for _, connection := range status.Connections {
				client := connection.GetUser()
				write(conn, "%s:%s:%s\n", client.Name, client.Addr)
			}

		// TODO remove
		case "GET_SERVER_STATUS":
			writeln(conn, "OK")

		default:
			fmt.Fprintf(conn, "ERROR: unknown command: %s\n", line)
		}
	}
}

func (s *BackendHandler) GetWhitelist(username string) []*entity.WhitelistEntry {
	entries, err := s.db.GetUserWhitelistEntries(username)
	if err != nil {
		return nil
	}
	return entries
}

func (s *BackendHandler) SetWhitelist(whitelistName string, usernames ...string) error {
	return s.db.ChangeUsersWhitelist(whitelistName, usernames...)
}

func (s *BackendHandler) AddWhitelistEntry(whitelistName, entry string) error {
	return s.db.AddWhitelistEntry(entry, whitelistName)
}

func (s *BackendHandler) DeleteWhitelistEntry(whitelistName, entry string) error {
	return s.db.DeleteWhitelistEntry(entry, whitelistName)
}

func (s *BackendHandler) GetConnections() []*connection.Connection {
	return s.connManager.GetAllConnections()
}

func (s *BackendHandler) ConnectUser(name string, addr string) error {
	user, err := s.db.GetUserByName(name)
	if err != nil {
		return errors.New("user not found")
	}

	if _, err := s.connManager.GetConnection(name); err == nil {
		return errors.New("user already connected")
	}

	if err := s.db.ChangeUserAddr(name, addr); err != nil {
		return err
	}

	_, err = s.connManager.NewConnection(user)
	return err
}

func (s *BackendHandler) DisconnectUser(name string) error {
	return s.connManager.DeleteConnection(name)
}

func (s *BackendHandler) RefreshConnection(name string) error {
	return s.connManager.RefreshConnection(name)
}

func (s *BackendHandler) ChangeUserName(oldName, newName string) error {
	if _, err := s.connManager.GetConnection(oldName); err != nil {
		return errors.New("user not connected")
	}

	if _, err := s.db.GetUserByName(newName); err == nil {
		return errors.New("name already in use")
	}

	if err := s.db.ChangeUserName(oldName, newName); err != nil {
		return err
	}

	conn, err := s.connManager.GetConnection(oldName)
	if err != nil {
		return nil
	}

	s.connManager.DeleteConnection(oldName)
	user := conn.GetUser()
	user.Name = newName
	s.connManager.NewConnection(&user)

	return nil
}

func (s *BackendHandler) DeleteUsers(usernames ...string) error {
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

func (h *BackendHandler) RegisterPendingUser(username, addr string) error {
	user, err := h.pendingUsers.Pop(username)
	if err != nil {
		return err
	}
	if user.Addr != addr {
		user.Addr = addr
	}
	if _, err := h.db.CreateUser(user.Name, user.Addr); err != nil {
		return err
	}
	return nil
}

func (s *BackendHandler) Pair(username string) (*connection.Connection, error) {
	user, err := s.db.GetUserByName(username)
	if err != nil {
		return nil, err
	}
	return s.connManager.NewConnection(user)
}

func (s *BackendHandler) Forget(username string) error {
	return s.connManager.DeleteConnection(username)
}

var startTime = time.Now()

func (s *BackendHandler) GetStatus() *ServerStatus {
	return &ServerStatus{
		Connections: s.connManager.GetAllConnections(),
		Uptime:      time.Since(startTime),
	}
}
