//go:build windows

package rpc

import (
	"errors"
	"fmt"
	"io"
	"net"
	"time"

	"bigbrother_server_backend/packages/application/settings"
	"bigbrother_server_backend/packages/domain/entity"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database"
	"bigbrother_server_backend/packages/infrastructure/notification"
	"bigbrother_server_backend/packages/infrastructure/pending"
)

type BackendHandler struct {
	db           database.DBInstance
	connManager  connection.Manager
	pendingUsers *pending.UserStorage
	startTime    time.Time
	bus          *notification.Manager
}

func NewBackendHandler(
	db database.DBInstance,
	connManager connection.Manager,
	pendingUsers *pending.UserStorage,
	bus *notification.Manager,
) *BackendHandler {
	return &BackendHandler{
		db:           db,
		connManager:  connManager,
		pendingUsers: pendingUsers,
		startTime:    time.Now(),
		bus:          bus,
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

	scanner := newScanner(conn)
	for {
		conn.SetReadDeadline(time.Now().Add(requestReadTimeout))
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
				h.bus.Publish(notification.Event{
					Type: notification.PendingAdded,
					Data: map[string]string{"name": args[0], "addr": addr},
				})
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
				h.bus.Publish(notification.Event{
					Type: notification.UserConnected,
					Data: map[string]string{"name": args[0], "addr": addr},
				})
				writeOK(conn)
			}

		case "DISCONNECT":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			if err := h.connManager.DeleteConnection(args[0]); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				h.bus.Publish(notification.Event{
					Type: notification.UserDisconnected,
					Data: map[string]string{"name": args[0]},
				})
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
				name = settingsapplication.DefaultServerName
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
			h.bus.Publish(notification.Event{
				Type: notification.FiltrationToggled,
				Data: map[string]string{"enabled": args[0]},
			})
			writeOK(conn)

		case "SUBSCRIBE":
			name := ""
			if len(args) >= 1 {
				name = args[0]
			}
			sub := notification.NewSubscriber(conn, 4096)
			writeOK(conn)
			h.bus.Add(sub)
			conn.SetReadDeadline(time.Time{}) // pushed events/heartbeats, no request deadline
			scanner := newScanner(conn)
			for scanner.Scan() {
				line := scanner.Text()
				if line == "" {
					continue
				}
				// Keep connection alive until client disconnects
				if name != "" {
					h.RefreshConnection(name)
				}
			}
			h.bus.Remove(sub)
			if name != "" {
				h.connManager.DeleteConnection(name)
			}
			return

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

func (s *BackendHandler) DeleteUsers(usernames ...string) error {
	return deleteUsers(s.db, s.connManager, usernames...)
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

func (s *BackendHandler) RefreshConnection(name string) error {
	return s.connManager.RefreshConnection(name)
}

func (s *BackendHandler) RegisterPendingUser(username, addr string) error {
	log.Info("Registration request for user \""+username+"\"", nil)
	return s.pendingUsers.Add(username, addr)
}

func (s *BackendHandler) GetStatus() *ServerStatus {
	return &ServerStatus{
		Connections: s.connManager.GetAllConnections(),
		Uptime:      time.Since(s.startTime),
	}
}
