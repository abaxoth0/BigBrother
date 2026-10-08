//go:build windows

package rpc

import (
	"crypto/subtle"
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
			if len(args) < 2 {
				writeErrorTLV(conn, "Missing username or token")
				continue
			}
			if !h.authenticate(args[0], args[1]) {
				writeErrorTLV(conn, "invalid token")
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
			signPublic := ""
			if len(args) >= 3 {
				signPublic = args[2]
			}
			if err := h.RegisterPendingUser(args[0], addr, signPublic); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				h.bus.Publish(notification.Event{
					Type: notification.PendingAdded,
					Data: map[string]string{"name": args[0], "addr": addr},
				})
				writeOK(conn)
			}

		case "CONNECT":
			if len(args) < 3 {
				writeErrorTLV(conn, "Missing name, address or token")
				continue
			}
			if !h.authenticate(args[0], args[2]) {
				writeErrorTLV(conn, "invalid token")
				continue
			}
			addr := args[1]
			if addr == "" {
				addr = conn.RemoteAddr().String()
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
			if len(args) < 2 {
				writeErrorTLV(conn, "Missing name or token")
				continue
			}
			if !h.authenticate(args[0], args[1]) {
				writeErrorTLV(conn, "invalid token")
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
			if len(args) < 2 {
				writeErrorTLV(conn, "Missing name or token")
				continue
			}
			if !h.authenticate(args[0], args[1]) {
				writeErrorTLV(conn, "invalid token")
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

		case "GET_TOKEN_CHALLENGE":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			if _, err := h.db.GetUserByName(args[0]); err != nil {
				writeErrorTLV(conn, "user not found")
				continue
			}
			nonce, err := newChallenge(args[0])
			if err != nil {
				writeErrorTLV(conn, "failed to issue challenge")
				continue
			}
			writeTLVResponse(conn, nonce)

		case "GET_TOKEN":
			if len(args) < 3 {
				writeErrorTLV(conn, "Missing name, challenge or signature")
				continue
			}
			if !consumeChallenge(args[0], args[1]) {
				writeErrorTLV(conn, "invalid or expired challenge")
				continue
			}
			u, err := h.db.GetUserByName(args[0])
			if err != nil {
				writeErrorTLV(conn, "user not found")
				continue
			}
			if u.SignPublic == "" {
				writeErrorTLV(conn, "client key not registered")
				continue
			}
			if !verifyClientSignature(u.SignPublic, args[0], args[1], args[2]) {
				writeErrorTLV(conn, "signature verify failed")
				continue
			}
			if u.Token == "" {
				writeTLVResponse(conn, "NOT_SET")
				continue
			}
			writeTLVResponse(conn, u.Token)

		case "SUBSCRIBE":
			if len(args) < 2 {
				writeErrorTLV(conn, "Missing name or token")
				continue
			}
			name := args[0]
			if !h.authenticate(name, args[1]) {
				writeErrorTLV(conn, "invalid token")
				continue
			}
			sub := notification.NewSubscriber(conn, 4096)
			if err := h.bus.Add(sub); err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			writeOK(conn)
			conn.SetReadDeadline(time.Time{}) // pushed events/heartbeats, no request deadline
			scanner := newScanner(conn)
			for scanner.Scan() {
				line := scanner.Text()
				if line == "" {
					continue
				}
				// Keep connection alive until client disconnects
				h.RefreshConnection(name)
			}
			h.bus.Remove(sub)
			h.connManager.DeleteConnection(name)
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

// authenticate verifies a client-supplied per-user token (constant-time). Users
// that predate token issuance (empty token) are rejected until re-approved.
func (s *BackendHandler) authenticate(username, token string) bool {
	u, err := s.db.GetUserByName(username)
	if err != nil || u.Token == "" {
		return false
	}
	return subtle.ConstantTimeCompare([]byte(u.Token), []byte(token)) == 1
}

func (s *BackendHandler) RegisterPendingUser(username, addr, signPublic string) error {
	log.Info("Registration request for user \""+username+"\"", nil)
	return s.pendingUsers.Add(username, addr, signPublic)
}

func (s *BackendHandler) GetStatus() *ServerStatus {
	return &ServerStatus{
		Connections: s.connManager.GetAllConnections(),
		Uptime:      time.Since(s.startTime),
	}
}
