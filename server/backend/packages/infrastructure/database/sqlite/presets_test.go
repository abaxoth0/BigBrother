package sqlite

import (
	"bigbrother_server_backend/packages/domain/entity"
	"path/filepath"
	"reflect"
	"testing"
	"time"
)

func checkPresetStates(t *testing.T, db *Database, active ...string) {
	t.Helper()
	want := map[string]bool{}
	for _, name := range active {
		want[name] = true
	}
	rows, err := db.GetWhitelists()
	if err != nil {
		t.Fatal(err)
	}
	for _, row := range rows {
		if row.IsActive != want[row.Name] {
			t.Fatalf("%s active=%v, want %v", row.Name, row.IsActive, want[row.Name])
		}
		delete(want, row.Name)
	}
	if len(want) > 0 {
		t.Fatalf("missing whitelists: %v", want)
	}
}

func TestWhitelistPresetLifecycle(t *testing.T) {
	path := filepath.Join(t.TempDir(), "presets.db")
	db := New(path)
	mustTimer(t, db.Connect())
	defer func() { db.Disconnect() }()
	for _, name := range []string{"work", "study", "games"} {
		mustTimer(t, db.CreateWhitelist(name, ""))
	}
	mustTimer(t, db.SetWhitelistActive("games", true))
	mustTimer(t, db.SaveWhitelistPreset("Работа", []string{"work", "study"}, false))
	mustTimer(t, db.SaveWhitelistPreset("Пустой", nil, false))
	checkPresetStates(t, db, "games")
	mustTimer(t, db.Disconnect())
	mustTimer(t, db.Connect())
	presets, err := db.GetWhitelistPresets()
	mustTimer(t, err)
	want := []entity.WhitelistPreset{
		{Name: "Пустой", ActiveWhitelists: []string{}},
		{Name: "Работа", ActiveWhitelists: []string{"study", "work"}},
	}
	if !reflect.DeepEqual(presets, want) {
		t.Fatalf("persisted preview: %+v, want %+v", presets, want)
	}
	changed, err := db.LoadWhitelistPreset("Работа")
	mustTimer(t, err)
	if !changed {
		t.Fatal("load did not report a change")
	}
	checkPresetStates(t, db, "work", "study")
	changed, err = db.LoadWhitelistPreset("Работа")
	mustTimer(t, err)
	if changed {
		t.Fatal("repeated load reported a change")
	}
	mustTimer(t, db.ChangeWhitelistName("work", "renamed"))
	mustTimer(t, db.DeleteWhitelist("study"))
	mustTimer(t, db.CreateWhitelist("study", ""))
	mustTimer(t, db.CreateWhitelist("new", ""))
	mustTimer(t, db.SetWhitelistActive("new", true))
	_, err = db.LoadWhitelistPreset("Работа")
	mustTimer(t, err)
	checkPresetStates(t, db, "renamed")
	presets, err = db.GetWhitelistPresets()
	mustTimer(t, err)
	if !reflect.DeepEqual(presets[1].ActiveWhitelists, []string{"renamed"}) {
		t.Fatalf("rename/delete preview: %+v", presets)
	}
	_, err = db.LoadWhitelistPreset("Пустой")
	mustTimer(t, err)
	checkPresetStates(t, db)
}

func TestWhitelistPresetRollback(t *testing.T) {
	db := newTestDatabase(t)
	mustTimer(t, db.CreateWhitelist("first", ""))
	mustTimer(t, db.CreateWhitelist("second", ""))
	mustTimer(t, db.SaveWhitelistPreset("saved", []string{"first"}, false))
	for _, test := range []struct {
		name    string
		lists   []string
		replace bool
	}{
		{"saved", []string{"second"}, false},
		{"saved", []string{"second", "missing"}, true},
		{"saved", []string{"second", "second"}, true},
		{"invalid", []string{"missing"}, false},
		{" \t", nil, false},
		{"bad\nname", nil, false},
	} {
		if err := db.SaveWhitelistPreset(test.name, test.lists, test.replace); err == nil {
			t.Fatalf("invalid save succeeded: %+v", test)
		}
	}
	presets, err := db.GetWhitelistPresets()
	mustTimer(t, err)
	if len(presets) != 1 || !reflect.DeepEqual(presets[0].ActiveWhitelists, []string{"first"}) {
		t.Fatalf("failed save changed presets: %+v", presets)
	}
	mustTimer(t, db.SaveWhitelistPreset("saved", []string{"second"}, true))
	mustTimer(t, db.SetWhitelistActive("first", true))
	if _, err := db.LoadWhitelistPreset("missing"); err == nil {
		t.Fatal("missing preset accepted")
	}
	checkPresetStates(t, db, "first")
	_, err = db.conn.Exec(`CREATE TRIGGER fail_activation BEFORE UPDATE OF is_active ON whitelist
        WHEN NEW.name = 'second' BEGIN SELECT RAISE(ABORT, 'write failure'); END`)
	mustTimer(t, err)
	if _, err := db.LoadWhitelistPreset("saved"); err == nil {
		t.Fatal("activation failure hidden")
	}
	checkPresetStates(t, db, "first")
}

func TestWhitelistPresetsPreserveTimers(t *testing.T) {
	db, advance := timerDatabase(t)
	mustTimer(t, db.CreateTemporaryWhitelist("temporary", 900, false))
	mustTimer(t, db.CreateTemporaryWhitelist("expiring", 10, true))
	mustTimer(t, db.CreateWhitelist("regular", ""))
	mustTimer(t, db.SaveWhitelistPreset("on", []string{"regular", "temporary", "expiring"}, false))
	mustTimer(t, db.SaveWhitelistPreset("off", nil, false))
	_, err := db.LoadWhitelistPreset("on")
	mustTimer(t, err)
	advance(5 * time.Second)
	_, err = db.LoadWhitelistPreset("on")
	mustTimer(t, err)
	timerRow(t, db, "temporary", true, 900, 895)
	advance(6 * time.Second)
	_, err = db.LoadWhitelistPreset("off")
	mustTimer(t, err)
	timerRow(t, db, "temporary", false, 900, 889)
	advance(time.Hour)
	_, err = db.LoadWhitelistPreset("on")
	mustTimer(t, err)
	timerRow(t, db, "temporary", true, 900, 889)
	checkPresetStates(t, db, "regular", "temporary")
	presets, err := db.GetWhitelistPresets()
	mustTimer(t, err)
	if !reflect.DeepEqual(presets[1].ActiveWhitelists, []string{"regular", "temporary"}) {
		t.Fatalf("expired list retained in preview: %+v", presets)
	}
}

func TestDeleteWhitelistPreset(t *testing.T) {
	db, advance := timerDatabase(t)
	mustTimer(t, db.CreateWhitelist("regular", ""))
	mustTimer(t, db.CreateTemporaryWhitelist("temporary", 900, false))
	mustTimer(t, db.SaveWhitelistPreset("delete", []string{"regular", "temporary"}, false))
	mustTimer(t, db.SaveWhitelistPreset("keep", []string{"regular"}, false))
	_, err := db.LoadWhitelistPreset("delete")
	mustTimer(t, err)
	advance(time.Minute)
	mustTimer(t, db.DeleteWhitelistPreset("delete"))
	mustTimer(t, db.DeleteWhitelistPreset("delete"))
	checkPresetStates(t, db, "regular", "temporary")
	timerRow(t, db, "temporary", true, 900, 840)
	if _, err := db.LoadWhitelistPreset("delete"); err == nil {
		t.Fatal("deleted preset can still be loaded")
	}
	var members int
	mustTimer(t, db.conn.QueryRow("SELECT COUNT(*) FROM whitelist_preset_member WHERE preset_name = ?", "delete").Scan(&members))
	if members != 0 {
		t.Fatal("deleted preset retained memberships")
	}
	presets, err := db.GetWhitelistPresets()
	mustTimer(t, err)
	want := []entity.WhitelistPreset{{Name: "keep", ActiveWhitelists: []string{"regular"}}}
	if !reflect.DeepEqual(presets, want) {
		t.Fatalf("deletion affected another preset: %+v", presets)
	}
	_, err = db.conn.Exec(`CREATE TRIGGER fail_preset_delete BEFORE DELETE ON whitelist_preset
        BEGIN SELECT RAISE(ABORT, 'write failure'); END`)
	mustTimer(t, err)
	if err := db.DeleteWhitelistPreset("keep"); err == nil {
		t.Fatal("preset deletion failure hidden")
	}
	presets, err = db.GetWhitelistPresets()
	mustTimer(t, err)
	if !reflect.DeepEqual(presets, want) {
		t.Fatalf("failed deletion removed memberships: %+v", presets)
	}
	mustTimer(t, db.Disconnect())
	mustTimer(t, db.Connect())
	presets, err = db.GetWhitelistPresets()
	mustTimer(t, err)
	if !reflect.DeepEqual(presets, want) {
		t.Fatalf("deleted preset returned after restart: %+v", presets)
	}
}
