//go:build windows

package rpc

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"time"

	"bigbrother_server_backend/packages/domain/entity"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database"
	"bigbrother_server_backend/packages/infrastructure/pending"
)

type ServerStatus struct {
	Connections []*connection.Connection
	Uptime      time.Duration
}

type FrontendHandler struct {
	db           database.DBInstance
	connManager  connection.Manager
	activeWl 	  string // TODO refactor?
	pendingUsers *pending.UserStorage
	startTime    time.Time
}

func NewFrontendHandler(
	db database.DBInstance,
	connManager connection.Manager,
	pendingUsers *pending.UserStorage,
) *FrontendHandler {
	activeWl, _ := db.GetSetting("active_whitelist")
	return &FrontendHandler{
		db:           db,
		connManager:  connManager,
		pendingUsers: pendingUsers,
		activeWl:     activeWl,
		startTime:    time.Now(),
	}
}

func (h *FrontendHandler) handle(conn net.Conn) {
	defer conn.Close()
	log.Info("Frontend connected", nil)

	scanner := bufio.NewScanner(conn)
	for {
		cmd, args, err := readRequest(scanner)
		if err != nil {
			if err != io.EOF {
				writeErrorTLV(conn, err.Error())
			}
			return
		}

		log.Debug("Frontend received: "+cmd, nil)

		switch cmd {
		case "GET_ACTIVE_WHITELIST":
			writeTLVResponse(conn, h.GetActiveWhitelist())

		case "SET_ACTIVE_WHITELIST":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing whitelist name")
				continue
			}
			h.SetActiveWhitelist(args[0])
			writeOK(conn)

		case "GET_SERVER_STATUS":
			connections := h.connManager.GetAllConnections()
			d := time.Since(h.startTime)
			hours := int(d.Hours())
			minutes := int(d.Minutes()) % 60
			uptimeStr := fmt.Sprintf("%dh %dm", hours, minutes)
			status := fmt.Sprintf("uptime:%s:clients:%d:pending:%d",
				uptimeStr,
				len(connections),
				len(h.pendingUsers.GetAll()))
			writeTLVResponse(conn, status)

		case "APPROVE":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			log.Info("Approving user \""+args[0]+"\"...", nil)
			if err := h.ApproveUser(args[0]); err != nil {
				log.Error("Approving user \""+args[0]+"\"", err.Error(), nil)
				writeErrorTLV(conn, err.Error())
			} else {
				log.Info("Approving user \""+args[0]+"\": OK", nil)
				writeOK(conn)
			}

		case "REJECT":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			h.RejectUser(args[0])
			writeOK(conn)

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

		case "DELETE_USERS":
			if len(args) < 1 {
				writeErrorTLV(conn, "Missing name")
				continue
			}
			if err := h.DeleteUsers(args...); err != nil {
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
				data = append(data, fmt.Sprintf("%s:%d", wl.Name, entryCount))
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
				writeOK(conn)
			}

		case "GET_SERVER_NAME":
			name, _ := h.db.GetSetting("server_name")
			if name == "" {
				name = "BigBrother Server"
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
			h.db.SetSetting("server_port", args[0])
			writeOK(conn)

		case "GET_LOG_PATH":
			writeTLVResponse(conn, h.getLogPath())

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
	if err := h.db.DeleteWhitelist(name); err != nil {
		return err
	}
	if h.activeWl == name {
		h.activeWl = ""
		h.db.SetSetting("active_whitelist", "")
	}
	return nil
}

func (h *FrontendHandler) ChangeWhitelistName(oldName, newName string) error {
	if err := h.db.ChangeWhitelistName(oldName, newName); err != nil {
		return err
	}
	if h.activeWl == oldName {
		h.activeWl = newName
		h.db.SetSetting("active_whitelist", newName)
	}
	return nil
}

func (h *FrontendHandler) SetWhitelistEntries(name string, entries []string) error {
	existingEntries, err := h.db.GetWhitelistEntries(name)
	if err != nil {
		return err
	}

	existingSet := make(map[string]bool)
	for _, e := range existingEntries {
		existingSet[e.Value] = true
	}

	newSet := make(map[string]bool)
	for _, e := range entries {
		newSet[e] = true
	}

	for _, e := range existingEntries {
		if !newSet[e.Value] {
			// TODO put this in a transaction
			if err := h.db.DeleteWhitelistEntry(e.Value, name); err != nil {
				return err
			}
		}
	}

	// TODO union this two methods

	for _, e := range entries {
		if !existingSet[e] {
			// TODO put this in a transaction
			if err := h.db.AddWhitelistEntry(e, name); err != nil {
				return err
			}
		}
	}

	return nil
}

func (h *FrontendHandler) ApproveUser(name string) error {
	user, err := h.pendingUsers.Pop(name)
	if err != nil {
		return err
	}
	if _, err := h.db.CreateUser(user.Name, user.Addr); err != nil {
		return err
	}
	return nil
}

func (h *FrontendHandler) RejectUser(name string) {
	h.pendingUsers.Pop(name) // ignore error, user just won't be approved
}

func (h *FrontendHandler) DisconnectUser(name string) error {
	return h.connManager.DeleteConnection(name)
}

func (h *FrontendHandler) DeleteUsers(names ...string) error {
	if err := h.db.DeleteUsers(names...); err != nil {
		return err
	}
	errMsgs := make([]string, 0, len(names))
	for _, name := range names {
		if err := h.connManager.DeleteConnection(name); err != nil {
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

func (h *FrontendHandler) GetActiveWhitelist() string {
	return h.activeWl
}

func (h *FrontendHandler) SetActiveWhitelist(name string) {
	h.activeWl = name
	h.db.SetSetting("active_whitelist", name)
}

func (h *FrontendHandler) getLogPath() string {
	exe, err := os.Executable()
	if err == nil {
		return filepath.Join(filepath.Dir(exe), "logs", "server")
	}
	return filepath.Join(".", "logs", "server")
}
