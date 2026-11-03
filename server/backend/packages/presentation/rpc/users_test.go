//go:build windows

package rpc

import (
	"encoding/json"
	"errors"
	"path/filepath"
	"strings"
	"testing"

	"bigbrother_server_backend/packages/domain/entity"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
)

func TestFrontendRegisteredUsers(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "users.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	manager := connection.NewMemoryResidentConnectionManager()
	defer manager.Stop()
	frontend := NewFrontendHandler(db, manager, nil, nil)
	if response := handlerResponse(t, frontend, "GET_REGISTERED_USERS\n\n"); response != "OK\n2\n[]\n\n" {
		t.Fatalf("empty users: %q", response)
	}
	for name, address := range map[string]string{"online": "10.0.0.1", "offline:клиент": "[2001:db8::2]:1984"} {
		if _, err := db.CreateUser(name, address); err != nil {
			t.Fatal(err)
		}
		if err := db.SetUserToken(name, "private-token"); err != nil {
			t.Fatal(err)
		}
		if err := db.SetUserSignPublic(name, "private-key-metadata"); err != nil {
			t.Fatal(err)
		}
	}
	user, err := db.GetUserByName("online")
	if err != nil {
		t.Fatal(err)
	}
	manager.NewConnection(user)
	response := handlerResponse(t, frontend, "GET_REGISTERED_USERS\n\n")
	lines := strings.Split(response, "\n")
	var users []map[string]string
	if len(lines) < 3 || json.Unmarshal([]byte(lines[2]), &users) != nil || len(users) != 2 {
		t.Fatalf("registered users: %q", response)
	}
	if users[0]["name"] != "offline:клиент" || users[0]["address"] != "[2001:db8::2]:1984" || users[1]["name"] != "online" {
		t.Fatalf("offline users, names, addresses or ordering lost: %v", users)
	}
	for _, item := range users {
		if len(item) != 2 {
			t.Fatalf("unexpected user fields: %v", item)
		}
	}
	if strings.Contains(response, "private-") {
		t.Fatal("registry response exposed credentials")
	}
	backend := NewBackendHandler(db, manager, nil, nil)
	if response := handlerResponse(t, backend, "GET_REGISTERED_USERS\n\n"); !strings.HasPrefix(response, "ERROR\n") {
		t.Fatalf("remote client read the registry: %q", response)
	}
	failed := NewFrontendHandler(failedUsersDB{DBInstance: db}, manager, nil, nil)
	if response := handlerResponse(t, failed, "GET_REGISTERED_USERS\n\n"); !strings.HasPrefix(response, "ERROR\n") {
		t.Fatalf("database error returned a successful registry: %q", response)
	}
}

type failedUsersDB struct{ database.DBInstance }

func (failedUsersDB) GetUsers() ([]*entity.User, error) {
	return nil, errors.New("database unavailable")
}
