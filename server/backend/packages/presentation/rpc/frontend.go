//go:build windows

package rpc

import (
	buildlog "bigbrother_server_backend/packages/common/log"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"time"

	"bigbrother_server_backend/packages/application/settings"
	"bigbrother_server_backend/packages/application/updates"
	whitelistapplication "bigbrother_server_backend/packages/application/whitelist"
	"bigbrother_server_backend/packages/domain/entity"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database"
	"bigbrother_server_backend/packages/infrastructure/notification"
	"bigbrother_server_backend/packages/infrastructure/pending"
)

type ServerStatus struct {
	Connections []*connection.Connection
	Uptime      time.Duration
}

type FrontendHandler struct {
	db                     database.DBInstance
	connManager            connection.Manager
	pendingUsers           *pending.UserStorage
	startTime              time.Time
	bus                    *notification.Manager
	certificateFingerprint string
}

func NewFrontendHandler(
	db database.DBInstance,
	connManager connection.Manager,
	pendingUsers *pending.UserStorage,
	bus *notification.Manager,
	fingerprint ...string,
) *FrontendHandler {
	activeFingerprint := ""
	if len(fingerprint) > 0 {
		activeFingerprint = fingerprint[0]
	}
	return &FrontendHandler{
		db:                     db,
		connManager:            connManager,
		pendingUsers:           pendingUsers,
		startTime:              time.Now(),
		bus:                    bus,
		certificateFingerprint: activeFingerprint,
	}
}

func (h *FrontendHandler) handle(conn net.Conn) {
	defer conn.Close()

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

		if buildlog.DiagnosticsEnabled {
			log.Debug("Frontend received: "+cmd, nil)
		}

		switch cmd {
		case "SYNC_WHITELISTS":
			if len(args) != 0 {
				writeErrorTLV(conn, "Expected no arguments")
				continue
			}
			composition, err := whitelistapplication.Compose(h.db)
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			h.bus.Publish(notification.Event{Type: notification.WhitelistSyncRequested,
				Data: map[string]string{"revision": whitelistapplication.Revision(composition.Entries)}})
			writeOK(conn)

		case "SET_CLIENT_IPV6_BLOCK":
			if len(args) != 2 || (args[1] != "0" && args[1] != "1") {
				writeErrorTLV(conn, "Expected client name and IPv6 blocking value 0 or 1")
				continue
			}
			client, err := h.connManager.GetConnection(args[0])
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			if client.Policy == nil || !client.Policy.CanSetIpv6Block {
				writeErrorTLV(conn, "Client does not support remote IPv6 blocking changes")
				continue
			}
			h.bus.Publish(notification.Event{Type: notification.ClientIpv6BlockChanged,
				Data: map[string]string{"name": args[0], "enabled": args[1]}})
			writeOK(conn)

		case "SET_CLIENT_UPDATE":
			if len(args) != 1 {
				writeErrorTLV(conn, "Expected update metadata")
				continue
			}
			data := args[0]
			if data != "" {
				manifest, err := updates.Parse(data)
				if err != nil {
					writeErrorTLV(conn, err.Error())
					continue
				}
				canonical, _ := json.Marshal(manifest)
				data = string(canonical)
			}
			if err := h.db.SetSetting("client_update", data); err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			h.bus.Publish(notification.Event{Type: notification.ClientUpdateChanged, Data: map[string]string{}})
			writeOK(conn)

		case "GET_CLIENT_UPDATE":
			data, err := h.db.GetSetting("client_update")
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			if data == "" {
				writeOK(conn)
				continue
			}
			writeTLVResponse(conn, data)

		case "GET_COMPOSED_WHITELIST_DETAILS":
			composition, err := whitelistapplication.Compose(h.db)
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			data, err := json.Marshal(composition.Entries)
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			writeTLVResponse(conn, string(data))

		case "GET_COMPOSED_WHITELIST":
			composition, err := whitelistapplication.Compose(h.db)
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			var data []string
			for _, entry := range composition.Entries {
				data = append(data, entry.Value)
			}
			writeTLVResponse(conn, data...)

		case "GET_WHITELIST_CONFLICTS":
			composition, err := whitelistapplication.Compose(h.db)
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			data, err := json.Marshal(composition.Conflicts)
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			writeTLVResponse(conn, string(data))

		case "SET_WHITELISTS_ACTIVE":
			if len(args) == 0 || len(args)%2 != 0 {
				writeErrorTLV(conn, "Expected whitelist name and active value pairs")
				continue
			}
			states := make(map[string]bool, len(args)/2)
			valid := true
			for i := 0; i < len(args); i += 2 {
				_, duplicate := states[args[i]]
				if duplicate || (args[i+1] != "0" && args[i+1] != "1") {
					valid = false
					break
				}
				states[args[i]] = args[i+1] == "1"
			}
			if !valid {
				writeErrorTLV(conn, "Expected unique whitelist names and active values 0 or 1")
				continue
			}
			changed, err := h.db.SetWhitelistsActive(states)
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			if changed {
				h.bus.Publish(notification.Event{
					Type: notification.WhitelistChanged,
					Data: map[string]string{},
				})
			}
			writeOK(conn)

		case "SET_WHITELIST_ACTIVE":
			if len(args) != 2 || (args[1] != "0" && args[1] != "1") {
				writeErrorTLV(conn, "Expected whitelist name and active value 0 or 1")
				continue
			}
			if err := h.db.SetWhitelistActive(args[0], args[1] == "1"); err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			h.bus.Publish(notification.Event{
				Type: notification.WhitelistChanged,
				Data: map[string]string{},
			})
			writeOK(conn)

		case "GET_CERTIFICATE_FINGERPRINT":
			if h.certificateFingerprint == "" {
				writeErrorTLV(conn, "certificate fingerprint unavailable")
			} else {
				writeTLVResponse(conn, h.certificateFingerprint)
			}

		case "GET_SERVER_STATUS":
			connections := h.connManager.GetAllConnections()
			d := time.Since(h.startTime)
			hours := int(d.Hours())
			minutes := int(d.Minutes()) % 60
			uptimeStr := fmt.Sprintf("%dh %dm", hours, minutes)
			filt, _ := h.db.GetSetting("filtration_enabled")
			if filt == "" {
				filt = "1"
			}
			status := fmt.Sprintf("uptime:%s:clients:%d:pending:%d:filtration:%s",
				uptimeStr,
				len(connections),
				len(h.pendingUsers.GetAll()),
				filt)
			writeTLVResponse(conn, status)

		case "APPROVE":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			log.Info("Approving user \""+args[0]+"\"...", nil)
			if _, err := h.ApproveUser(args[0]); err != nil {
				log.Error("Approving user \""+args[0]+"\"", err.Error(), nil)
				writeErrorTLV(conn, err.Error())
			} else {
				// The token is NOT broadcast in the event — the client fetches it
				// itself via the signed GET_TOKEN challenge.
				h.bus.Publish(notification.Event{
					Type: notification.UserApproved,
					Data: map[string]string{"name": args[0]},
				})
				log.Info("Approving user \""+args[0]+"\": OK", nil)
				writeOK(conn)
			}

		case "GET_USER_TOKEN":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			token, err := h.GetUserToken(args[0])
			if err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				writeTLVResponse(conn, token)
			}

		case "REGENERATE_TOKEN":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			token, err := h.RegenerateUserToken(args[0])
			if err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				writeTLVResponse(conn, token)
			}

		case "REJECT":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			h.RejectUser(args[0])
			h.bus.Publish(notification.Event{
				Type: notification.UserRejected,
				Data: map[string]string{"name": args[0]},
			})
			writeOK(conn)

		case "DISCONNECT":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			if err := h.connManager.DisconnectConnection(args[0]); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				writeOK(conn)
			}

		case "DELETE_USERS":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			if err := deleteUsers(h.db, h.connManager, args...); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				writeOK(conn)
			}

		case "CHANGE_NAME":
			if len(args) < 2 {
				writeErrorTLV(conn, "Invalid format, use: CHANGE_NAME oldName newName")
				continue
			}
			if err := h.db.ChangeUserName(args[0], args[1]); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				writeOK(conn)
			}

		case "GET_CLIENTS":
			connections := h.connManager.GetAllConnections()
			var data []string
			for _, connection := range connections {
				client := connection.GetUser()
				data = append(data, fmt.Sprintf("%s:%s:%s", client.Name, client.Addr, "Active"))
			}
			writeTLVResponse(conn, data...)

		case "GET_REGISTERED_USERS":
			users, err := h.db.GetUsers()
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			type registeredUser struct {
				Name    string `json:"name"`
				Address string `json:"address"`
			}
			result := []registeredUser{}
			for _, user := range users {
				result = append(result, registeredUser{Name: user.Name, Address: user.Addr})
			}
			data, err := json.Marshal(result)
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			writeTLVResponse(conn, string(data))

		case "GET_CLIENTS_DETAILS":
			composition, err := whitelistapplication.Compose(h.db)
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			revision := whitelistapplication.Revision(composition.Entries)
			type clientDetails struct {
				Name             string                   `json:"name"`
				Address          string                   `json:"address"`
				LastActivity     time.Time                `json:"lastActivity"`
				Policy           *connection.PolicyStatus `json:"policy"`
				WhitelistCurrent *bool                    `json:"whitelistCurrent"`
			}
			clients := []clientDetails{}
			for _, conn := range h.connManager.GetAllConnections() {
				user := conn.GetUser()
				client := clientDetails{Name: user.Name, Address: user.Addr, LastActivity: conn.LastActivity, Policy: conn.Policy}
				if conn.Policy != nil && conn.Policy.WhitelistRevision != "" {
					current := conn.Policy.WhitelistRevision == revision
					client.WhitelistCurrent = &current
				}
				clients = append(clients, client)
			}
			data, err := json.Marshal(clients)
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			writeTLVResponse(conn, string(data))

		case "GET_PENDING":
			var data []string
			for _, user := range h.pendingUsers.GetAll() {
				data = append(data, fmt.Sprintf("%s:%s:%d", user.Name, user.Addr, user.CreatedAt.Unix()))
			}
			writeTLVResponse(conn, data...)

		case "GET_WHITELISTS":
			wls, err := h.GetWhitelists()
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			var data []string
			for _, wl := range wls {
				entryCount := 0
				entries, err := h.db.GetWhitelistEntries(wl.Name)
				if err == nil {
					entryCount = len(entries)
				}
				data = append(data, fmt.Sprintf("%s:%d:%t", wl.Name, entryCount, wl.IsActive))
			}
			writeTLVResponse(conn, data...)

		case "GET_WHITELIST":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing whitelist name")
				continue
			}
			wl := h.GetWhitelist(args[0])
			var data []string
			for _, entry := range wl {
				data = append(data, entry.Value)
			}
			writeTLVResponse(conn, data...)

		case "CREATE_WHITELIST":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing whitelist name")
				continue
			}
			if err := h.CreateWhitelist(args[0]); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				h.bus.Publish(notification.Event{
					Type: notification.WhitelistChanged,
					Data: map[string]string{},
				})
				writeOK(conn)
			}

		case "DELETE_WHITELIST":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing whitelist name")
				continue
			}
			if err := h.DeleteWhitelist(args[0]); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				h.bus.Publish(notification.Event{
					Type: notification.WhitelistChanged,
					Data: map[string]string{},
				})
				writeOK(conn)
			}

		case "RENAME_WHITELIST":
			if len(args) < 2 {
				writeErrorTLV(conn, "Invalid format, use: RENAME_WHITELIST oldName newName")
				continue
			}
			if err := h.ChangeWhitelistName(args[0], args[1]); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				h.bus.Publish(notification.Event{
					Type: notification.WhitelistChanged,
					Data: map[string]string{},
				})
				writeOK(conn)
			}

		case "SAVE_WHITELIST":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing whitelist name")
				continue
			}
			wlName := args[0]
			entries := args[1:] // rest are entries
			if err := h.SetWhitelistEntries(wlName, entries); err != nil {
				writeErrorTLV(conn, err.Error())
			} else {
				h.bus.Publish(notification.Event{
					Type: notification.WhitelistChanged,
					Data: map[string]string{},
				})
				writeOK(conn)
			}

		case "GET_SERVER_NAME":
			name, _ := h.db.GetSetting("server_name")
			if name == "" {
				name = settingsapplication.DefaultServerName
			}
			writeTLVResponse(conn, name)

		case "SET_SERVER_NAME":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing server name")
				continue
			}
			h.db.SetSetting("server_name", args[0])
			writeOK(conn)

		case "GET_SERVER_PORT":
			port, _ := h.db.GetSetting("server_port")
			if port == "" {
				port = "1984"
			}
			writeTLVResponse(conn, port)

		case "SET_SERVER_PORT":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing port number")
				continue
			}
			port, err := settingsapplication.ParseServerPort(args[0])
			if len(args) != 1 || err != nil {
				writeErrorTLV(conn, "Expected TCP port 1 through 65535")
				continue
			}
			if err := h.db.SetSetting("server_port", fmt.Sprint(port)); err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			writeOK(conn)

		case "GET_LOG_PATH":
			writeTLVResponse(conn, h.getLogPath())

		case "GET_WINDOWS_NOTIFICATIONS":
			value, err := h.db.GetSetting("windows_notifications_enabled")
			if err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			if value != "0" {
				value = "1"
			}
			writeTLVResponse(conn, value)

		case "SET_WINDOWS_NOTIFICATIONS":
			if len(args) != 1 || (args[0] != "0" && args[0] != "1") {
				writeErrorTLV(conn, "Expected Windows notification value 0 or 1")
				continue
			}
			if err := h.db.SetSetting("windows_notifications_enabled", args[0]); err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			writeOK(conn)

		case "GET_FILTRATION":
			filt, _ := h.db.GetSetting("filtration_enabled")
			if filt == "" {
				filt = "1"
			}
			writeTLVResponse(conn, filt)

		case "SET_FILTRATION":
			if len(args) != 1 || (args[0] != "0" && args[0] != "1") {
				writeErrorTLV(conn, "Expected filtration value 0 or 1")
				continue
			}
			if err := h.db.SetSetting("filtration_enabled", args[0]); err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			h.bus.Publish(notification.Event{
				Type: notification.FiltrationToggled,
				Data: map[string]string{"enabled": args[0]},
			})
			writeOK(conn)

		case "SUBSCRIBE":
			sub := notification.NewSubscriber(conn, 65536)
			if err := h.bus.Add(sub); err != nil {
				writeErrorTLV(conn, err.Error())
				continue
			}
			log.Info("Frontend connected", nil)
			writeOK(conn)
			conn.SetReadDeadline(time.Time{}) // pushed events only, no request deadline
			scanner := newScanner(conn)
			for scanner.Scan() {
				// Keep connection alive until client disconnects
			}
			h.bus.Remove(sub)
			return

		default:
			writeErrorTLV(conn, fmt.Sprintf("unknown command: %s", cmd))
		}
	}
}

func (h *FrontendHandler) GetWhitelists() ([]*entity.Whitelist, error) {
	return h.db.GetWhitelists()
}

func (h *FrontendHandler) GetWhitelist(name string) []*entity.WhitelistEntry {
	entries, err := h.db.GetWhitelistEntries(name)
	if err != nil {
		return nil
	}
	return entries
}

func (h *FrontendHandler) CreateWhitelist(name string) error {
	return h.db.CreateWhitelist(name, "")
}

func (h *FrontendHandler) DeleteWhitelist(name string) error {
	return h.db.DeleteWhitelist(name)
}

func (h *FrontendHandler) ChangeWhitelistName(oldName, newName string) error {
	return h.db.ChangeWhitelistName(oldName, newName)
}

func (h *FrontendHandler) SetWhitelistEntries(name string, entries []string) error {
	return h.db.ReplaceWhitelistEntries(entries, name)
}

func (h *FrontendHandler) ApproveUser(name string) (string, error) {
	user, err := h.pendingUsers.Pop(name)
	if err != nil {
		return "", err
	}
	if _, err := h.db.CreateUser(user.Name, user.Addr); err != nil {
		return "", err
	}
	// Bind the client's public identity key from this registration. Approval is
	// the trust decision, so the key from the approved registration is used
	// (this also lets a re-registered existing user rebind a fresh key).
	if user.SignPublic != "" {
		if err := h.db.SetUserSignPublic(user.Name, user.SignPublic); err != nil {
			return "", err
		}
	}
	token := issueToken()
	if err := h.db.SetUserToken(user.Name, token); err != nil {
		return "", err
	}
	return token, nil
}

func (h *FrontendHandler) RejectUser(name string) {
	h.pendingUsers.Pop(name) // ignore error, user just won't be approved
}

func (h *FrontendHandler) GetUserToken(name string) (string, error) {
	u, err := h.db.GetUserByName(name)
	if err != nil {
		return "", err
	}
	return u.Token, nil
}

func (h *FrontendHandler) RegenerateUserToken(name string) (string, error) {
	if _, err := h.db.GetUserByName(name); err != nil {
		return "", err
	}
	token := issueToken()
	if err := h.db.SetUserToken(name, token); err != nil {
		return "", err
	}
	return token, nil
}

func (h *FrontendHandler) getLogPath() string {
	exe, err := os.Executable()
	if err == nil {
		return filepath.Join(filepath.Dir(exe), "logs", "server")
	}
	return filepath.Join(".", "logs", "server")
}
