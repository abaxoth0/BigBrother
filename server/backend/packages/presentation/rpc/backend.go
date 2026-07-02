//go:build windows

package rpc

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"net"
	"time"

	"bigbrother_server_backend/packages/domain/entity"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database"
	"bigbrother_server_backend/packages/infrastructure/pending"
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
	defer func() {
		// Graceful close: send FIN before Close to avoid RST on Windows
		// (bufio.Scanner may buffer data, causing Close() to send RST)
		if tcp, ok := conn.(*net.TCPConn); ok {
			tcp.CloseWrite()
		}
		conn.Close()
	}()
	log.Info("Client connected", nil)

	scanner := bufio.NewScanner(conn)
	for {
		cmd, args, err := readRequest(scanner)
		if err != nil {
			if err != io.EOF {
				writeErrorTLV(conn, err.Error())
			}
			return
		}

		log.Debug("Received: "+cmd, nil)

		switch cmd {
		case "GET_WHITELIST":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing username")
				continue
			}
			if _, err := h.db.GetUserByName(args[0]); err != nil {
				writeErrorTLV(conn, "user not found")
				continue
			}
			wl := h.GetWhitelist(args[0])
			var data []string
			for _, entry := range wl {
				data = append(data, entry.Value)
			}
			writeTLVResponse(conn, data...)

		case "REGISTER":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			addr := conn.RemoteAddr().String()
			if len(args) >= 2 && args[1] != "" {
				addr = args[1]
			}
			if err := h.RegisterPendingUser(args[0], addr); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				writeOK(conn)
			}

		case "CONNECT":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			addr := conn.RemoteAddr().String()
			if len(args) >= 2 && args[1] != "" {
				addr = args[1]
			}
			if err := h.ConnectUser(args[0], addr); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				writeOK(conn)
			}

		case "DISCONNECT":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			if err := h.DisconnectUser(args[0]); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				writeOK(conn)
			}

		case "REFRESH":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			if err := h.RefreshConnection(args[0]); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				writeOK(conn)
			}

		case "GET_STATUS":
			status := h.GetStatus()
			var data []string
			data = append(data, fmt.Sprintf("uptime:%s", status.Uptime.String()))
			for _, connection := range status.Connections {
				client := connection.GetUser()
				data = append(data, fmt.Sprintf("%s:%s:%s", client.Name, client.Addr, "Active"))
			}
			writeTLVResponse(conn, data...)

		case "GET_SERVER_NAME":
			name, err := h.db.GetSetting("server_name")
			if err != nil || name == "" {
				name = "BigBrother Server"
			}
			writeTLVResponse(conn, name)

		case "PING":
			writeOK(conn)

		case "GET_FILTRATION":
			filt, _ := h.db.GetSetting("filtration_enabled")
			if filt == "" {
				filt = "1"
			}
			writeTLVResponse(conn, filt)

		case "SET_FILTRATION":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing value")
				continue
			}
			h.db.SetSetting("filtration_enabled", args[0])
			writeOK(conn)

		default:
			writeErrorTLV(conn, fmt.Sprintf("unknown command: %s", cmd))
		}
	}
}

func (s *BackendHandler) GetWhitelist(username string) []*entity.WhitelistEntry {
	if _, err := s.db.GetUserByName(username); err != nil {
		return nil
	}
	activeWl, err := s.db.GetSetting("active_whitelist")
	if err != nil || activeWl == "" {
		return nil
	}
	entries, err := s.db.GetWhitelistEntries(activeWl)
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
	user.Addr = addr
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
		return fmt.Errorf(msg)
	}
	return nil
}

func (s *BackendHandler) RegisterPendingUser(username, addr string) error {
	log.Info("Registration request for user \""+username+"\"", nil)
	return s.pendingUsers.Add(username, addr)
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
