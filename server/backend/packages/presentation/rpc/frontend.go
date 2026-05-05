//go:build windows

package rpc

import (
	"bufio"
	"fmt"
	"net"
	"strings"
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
	activeWl 	 string // TODO refactor?
	pendingUsers *pending.UserStorage
}

func NewFrontendHandler(
	db database.DBInstance,
	connManager connection.Manager,
	pendingUsers *pending.UserStorage,
) *FrontendHandler {
	return &FrontendHandler{
		db: db,
		connManager: connManager,
		pendingUsers: pendingUsers,
	}
}

func (h *FrontendHandler) handle(conn net.Conn) {
	defer conn.Close()
	log.Info("Frontend connected", nil)

	scanner := bufio.NewScanner(conn)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}

		log.Debug("Frontend received: "+line, nil)

		msg := strings.Split(line, ":")
		if len(msg) > 2 {
			write(conn, "ERROR: Invalid request syntax (':' duplication)\n")
			return
		}
		cmd, arg := msg[0], ""
		if len(msg) > 1 {
			arg = strings.TrimSpace(msg[1])
		}

		switch cmd {
		case "GET_ACTIVE_WHITELIST":
			writeln(conn, h.GetActiveWhitelist())

		case "SET_ACTIVE_WHITELIST":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing whitelist name")
				return
			}
			h.SetActiveWhitelist(arg)
			write(conn, "OK\n")


		case "GET_SERVER_STATUS":
			writeln(conn, "OK")

		case "APPROVE":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing name")
				return
			}
			if err := h.ApproveUser(arg); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "OK\n")

		case "REJECT":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing name")
				return
			}
			h.RejectUser(arg)
			write(conn, "OK\n")

		case "DISCONNECT":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing name")
				return
			}
			if err := h.DisconnectUser(arg); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "OK\n")

		case "DELETE_USERS":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing name")
				return
			}
			if err := h.DeleteUsers(arg); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "OK\n")

		// TODO refactor to CHANGE_USER_NAME
		case "CHANGE_NAME":
			// Format: CHANGE_NAME:oldName:newName
			parts := strings.Split(arg, ":")
			if len(parts) != 2 || strings.TrimSpace(parts[0]) == "" || strings.TrimSpace(parts[1]) == "" {
				writeError(conn, requestError, "Invalid format, use: CHANGE_NAME:oldName:newName")
				return
			}
			if err := h.db.ChangeUserName(parts[0], parts[1]); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "OK\n")

		case "GET_CLIENTS":
			connections := h.connManager.GetAllConnections()
			writeln(conn, "OK")
			for _, connection := range connections {
				client := connection.GetUser()
				write(conn, "%s:%s:%s\n", client.Name, client.Addr, "Active")
			}

		case "GET_PENDING":
			writeln(conn, "OK")
			for _, user := range h.pendingUsers.GetAll() {
				writeln(conn, "%s:%s", user.Name, user.Addr)
			}

		case "GET_WHITELISTS":
			wls, err := h.GetWhitelists()
			if err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			writeln(conn, "OK")
			for _, wl := range wls {
				entryCount := 0
				entries, err := h.GetWhitelistEntries(wl.ID)
				if err == nil {
					entryCount = len(entries)
				}
				writeln(conn, "%s:%d", wl.Name, entryCount)
			}

		case "GET_WHITELIST":
			wl := h.GetWhitelist(arg)
			writeln(conn, "OK")
			for _, entry := range wl {
				writeln(conn, entry.Value)
			}

		case "CREATE_WHITELIST":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing whitelist name")
				return
			}
			if err := h.CreateWhitelist(arg); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "OK\n")

		case "DELETE_WHITELIST":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing whitelist name")
				return
			}
			if err := h.DeleteWhitelist(arg); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "OK\n")

		case "RENAME_WHITELIST":
			parts := strings.Split(arg, ":")
			if len(parts) != 2 || strings.TrimSpace(parts[0]) == "" || strings.TrimSpace(parts[1]) == "" {
				writeError(conn, requestError, "Invalid format, use: RENAME_WHITELIST:oldName:newName")
				return
			}
			if err := h.ChangeWhitelistName(parts[0], parts[1]); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "OK\n")

		case "SAVE_WHITELIST":
			if strings.TrimSpace(arg) == "" {
				writeError(conn, requestError, "Missing whitelist name")
				return
			}
			var entries []string
			for scanner.Scan() {
				entry := strings.TrimSpace(scanner.Text())
				if entry == "" {
					break
				}
				entries = append(entries, entry)
			}
			if err := h.SetWhitelistEntries(arg, entries); err != nil {
				writeError(conn, internalError, err.Error())
				return
			}
			write(conn, "OK\n")

		default:
			fmt.Fprintf(conn, "ERROR: unknown command: %s\n", line)
		}
	}
}

func (h *FrontendHandler) GetWhitelists() ([]*entity.Whitelist, error) {
	return h.db.GetWhitelists()
}

func (h *FrontendHandler) GetWhitelistEntries(whitelistID string) ([]*entity.WhitelistEntry, error) {
	return h.db.GetWhitelistEntries(whitelistID)
}

func (h *FrontendHandler) GetWhitelist(name string) []*entity.WhitelistEntry {
	wl, err := h.db.GetWhitelistByName(name)
	if err != nil {
		return nil
	}
	entries, err := h.db.GetWhitelistEntries(wl.ID)
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
	wl, err := h.db.GetWhitelistByName(name)
	if err != nil {
		return err
	}

	existingEntries, err := h.db.GetWhitelistEntries(wl.ID)
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
}
