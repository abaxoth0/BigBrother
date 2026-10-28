//go:build windows

package rpc

import (
	"bigbrother_server_backend/packages/domain/entity"
	"bigbrother_server_backend/packages/infrastructure/database"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/infrastructure/notification"
	"errors"
	"fmt"
	"path/filepath"
	"strings"
	"testing"
)

func presetRequest(command string, args ...string) string {
	var request strings.Builder
	request.WriteString(command + "\n")
	for _, arg := range args {
		fmt.Fprintf(&request, "%d\n%s\n", len(arg), arg)
	}
	request.WriteString("\n")
	return request.String()
}

func TestFrontendWhitelistPresets(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "presets.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	for _, name := range []string{"first", "second", "outside"} {
		if err := db.CreateWhitelist(name, ""); err != nil {
			t.Fatal(err)
		}
	}
	if err := db.SetWhitelistActive("outside", true); err != nil {
		t.Fatal(err)
	}
	events := 0
	bus := notification.NewManagerWithNotifier(func(event notification.Event) {
		if event.Type != notification.WhitelistChanged {
			return
		}
		events++
		rows, err := db.GetWhitelists()
		if err != nil {
			t.Error(err)
		}
		for _, row := range rows {
			if row.IsActive != (row.Name != "outside") {
				t.Errorf("event before complete activation: %+v", row)
			}
		}
	})
	h := NewFrontendHandler(db, nil, nil, bus)
	if response := handlerResponse(t, h, presetRequest("SAVE_WHITELIST_PRESET", "Работа", "0", "first", "second")); response != "OK\n\n" {
		t.Fatalf("save response: %q", response)
	}
	if events != 0 {
		t.Fatal("saving a preset published a policy change")
	}
	response := handlerResponse(t, h, presetRequest("GET_WHITELIST_PRESETS"))
	if !strings.HasPrefix(response, "OK\n") || !strings.Contains(response, `"name":"Работа","activeWhitelists":["first","second"]`) {
		t.Fatalf("preview response: %q", response)
	}
	for i := 0; i < 2; i++ {
		if response := handlerResponse(t, h, presetRequest("LOAD_WHITELIST_PRESET", "Работа")); response != "OK\n\n" {
			t.Fatalf("load response: %q", response)
		}
		if events != 1 {
			t.Fatalf("expected one committed change event, got %d", events)
		}
	}
	for _, request := range []string{
		presetRequest("SAVE_WHITELIST_PRESET"),
		presetRequest("SAVE_WHITELIST_PRESET", "test", "invalid"),
		presetRequest("SAVE_WHITELIST_PRESET", "Работа", "0"),
		presetRequest("SAVE_WHITELIST_PRESET", "Работа", "1", "missing"),
		presetRequest("LOAD_WHITELIST_PRESET"),
		presetRequest("LOAD_WHITELIST_PRESET", "missing"),
		presetRequest("LOAD_WHITELIST_PRESET", "Работа", "extra"),
	} {
		if response := handlerResponse(t, h, request); !strings.HasPrefix(response, "ERROR\n") {
			t.Fatalf("invalid preset request accepted: %q", response)
		}
	}
	if events != 1 {
		t.Fatal("invalid request published policy change")
	}
	h = NewFrontendHandler(failedPresetDB{db}, nil, nil, bus)
	for _, request := range []string{
		presetRequest("GET_WHITELIST_PRESETS"),
		presetRequest("SAVE_WHITELIST_PRESET", "test", "0"),
		presetRequest("LOAD_WHITELIST_PRESET", "Работа"),
	} {
		if response := handlerResponse(t, h, request); !strings.HasPrefix(response, "ERROR\n") {
			t.Fatalf("database failure hidden: %q", response)
		}
	}
	if events != 1 {
		t.Fatal("database failure published policy change")
	}
}

type failedPresetDB struct{ database.DBInstance }

func (failedPresetDB) GetWhitelistPresets() ([]entity.WhitelistPreset, error) {
	return nil, errors.New("preset read failed")
}

func (failedPresetDB) SaveWhitelistPreset(string, []string, bool) error {
	return errors.New("preset write failed")
}

func (failedPresetDB) LoadWhitelistPreset(string) (bool, error) {
	return false, errors.New("preset load failed")
}
