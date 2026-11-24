package sqlite

import (
	whitelistapplication "bigbrother_server_backend/packages/application/whitelist"
	"bigbrother_server_backend/packages/domain/entity"
	"database/sql"
	"errors"
	"math"
	"path/filepath"
	"testing"
	"time"
)

func timerDatabase(t *testing.T) (*Database, func(time.Duration)) {
	t.Helper()
	db := newTestDatabase(t)
	now := time.Date(2026, 10, 26, 12, 0, 0, 0, time.UTC)
	db.now = func() time.Time { return now }
	return db, func(elapsed time.Duration) { now = now.Add(elapsed) }
}

func timerRow(t *testing.T, db *Database, name string, active bool, duration, remaining float64) *entity.Whitelist {
	t.Helper()
	row, err := db.GetWhitelistByName(name)
	if err != nil {
		t.Fatal(err)
	}
	if row.Temporary == nil || row.IsActive != active || float64(row.Temporary.DurationSeconds) != duration || math.Abs(row.Temporary.RemainingSeconds-remaining) > 0.00001 {
		t.Fatalf("%s: active=%v timer=%+v, want active=%v duration=%v remaining=%v", name, row.IsActive, row.Temporary, active, duration, remaining)
	}
	return row
}

func mustTimer(t *testing.T, err error) {
	t.Helper()
	if err != nil {
		t.Fatal(err)
	}
}

func TestTemporaryWhitelistPauseResumeAndExpiry(t *testing.T) {
	db, advance := timerDatabase(t)
	mustTimer(t, db.CreateTemporaryWhitelist("lesson", 900, false))
	advance(time.Minute)
	timerRow(t, db, "lesson", false, 900, 900)
	mustTimer(t, db.SetWhitelistActive("lesson", true))
	advance(3*time.Minute + 250*time.Millisecond)
	timerRow(t, db, "lesson", true, 900, 719.75)
	mustTimer(t, db.SetWhitelistActive("lesson", false))
	advance(time.Hour)
	timerRow(t, db, "lesson", false, 900, 719.75)
	mustTimer(t, db.SetWhitelistActive("lesson", true))
	advance(time.Second)
	mustTimer(t, db.SetWhitelistActive("lesson", true))
	timerRow(t, db, "lesson", true, 900, 718.75)
	advance(719 * time.Second)
	changed, err := db.ExpireWhitelistTimers()
	mustTimer(t, err)
	if !changed {
		t.Fatal("expiry did not request client refresh")
	}
	timerRow(t, db, "lesson", false, 900, 0)
	changed, err = db.ExpireWhitelistTimers()
	mustTimer(t, err)
	if changed {
		t.Fatal("expiry repeated")
	}
	mustTimer(t, db.SetWhitelistActive("lesson", true))
	timerRow(t, db, "lesson", true, 900, 900)
}

func TestTemporaryWhitelistDurationAndRemainingEdits(t *testing.T) {
	db, advance := timerDatabase(t)
	mustTimer(t, db.CreateTemporaryWhitelist("lesson", 900, false))
	mustTimer(t, db.SetWhitelistActive("lesson", true))
	advance(3 * time.Minute)
	mustTimer(t, db.UpdateWhitelistTimer("lesson", 1200, nil, false))
	timerRow(t, db, "lesson", true, 1200, 1020)
	remaining := int64(1200)
	mustTimer(t, db.UpdateWhitelistTimer("lesson", 600, &remaining, false))
	timerRow(t, db, "lesson", true, 600, 1200)
	advance(time.Minute)
	mustTimer(t, db.SetWhitelistActive("lesson", false))
	mustTimer(t, db.UpdateWhitelistTimer("lesson", 900, nil, false))
	timerRow(t, db, "lesson", false, 900, 1440)
	mustTimer(t, db.ResetWhitelistTimers([]string{"lesson"}))
	timerRow(t, db, "lesson", false, 900, 900)
	mustTimer(t, db.SetWhitelistActive("lesson", true))
	advance(time.Minute)
	mustTimer(t, db.ResetWhitelistTimers([]string{"lesson"}))
	timerRow(t, db, "lesson", true, 900, 900)
	advance(time.Second)
	timerRow(t, db, "lesson", true, 900, 899)
}

func TestTemporaryWhitelistImmediateExpiryAndAutoDelete(t *testing.T) {
	for _, autoDelete := range []bool{false, true} {
		for _, editRemaining := range []bool{false, true} {
			db, advance := timerDatabase(t)
			mustTimer(t, db.CreateTemporaryWhitelist("lesson", 600, autoDelete))
			mustTimer(t, db.ReplaceWhitelistEntries([]string{"example.com"}, "lesson"))
			mustTimer(t, db.SetWhitelistActive("lesson", true))
			advance(7 * time.Minute)
			var remaining *int64
			zero := int64(0)
			if editRemaining {
				remaining = &zero
			}
			mustTimer(t, db.UpdateWhitelistTimer("lesson", 300, remaining, autoDelete))
			if autoDelete {
				if _, err := db.GetWhitelistByName("lesson"); !errors.Is(err, sql.ErrNoRows) {
					t.Fatalf("expired list still exists: %v", err)
				}
				for _, table := range []string{"whitelist_entry", "whitelist_timer"} {
					var count int
					mustTimer(t, db.conn.QueryRow("SELECT COUNT(*) FROM "+table).Scan(&count))
					if count != 0 {
						t.Fatalf("orphaned %s rows: %d", table, count)
					}
				}
			} else {
				timerRow(t, db, "lesson", false, 300, 0)
			}
		}
	}
}

func TestTemporaryWhitelistCompositionAndAutomaticDeletion(t *testing.T) {
	db, advance := timerDatabase(t)
	mustTimer(t, db.CreateWhitelist("permanent", ""))
	mustTimer(t, db.ReplaceWhitelistEntries([]string{"permanent.org"}, "permanent"))
	mustTimer(t, db.CreateTemporaryWhitelist("lesson", 5, true))
	mustTimer(t, db.ReplaceWhitelistEntries([]string{"example.com"}, "lesson"))
	_, err := db.SetWhitelistsActive(map[string]bool{"permanent": true, "lesson": true})
	mustTimer(t, err)
	composed, err := whitelistapplication.Compose(db)
	mustTimer(t, err)
	if len(composed.Entries) != 2 {
		t.Fatalf("temporary rules not composed: %+v", composed)
	}
	advance(5 * time.Second)
	changed, err := db.ExpireWhitelistTimers()
	mustTimer(t, err)
	if !changed {
		t.Fatal("expiry did not change policy")
	}
	composed, err = whitelistapplication.Compose(db)
	mustTimer(t, err)
	if len(composed.Entries) != 1 || composed.Entries[0].Value != "permanent.org" {
		t.Fatalf("expired access retained: %+v", composed.Entries)
	}
	rows, err := db.GetWhitelists()
	mustTimer(t, err)
	if len(rows) != 1 {
		t.Fatalf("auto-delete retained list: %+v", rows)
	}
}

func TestTemporaryWhitelistBatchRollbackAndRename(t *testing.T) {
	db, advance := timerDatabase(t)
	mustTimer(t, db.CreateTemporaryWhitelist("lesson", 900, false))
	mustTimer(t, db.CreateWhitelist("permanent", ""))
	if _, err := db.SetWhitelistsActive(map[string]bool{"lesson": true, "permanent": true, "zz-missing": true}); err == nil {
		t.Fatal("invalid batch accepted")
	}
	timerRow(t, db, "lesson", false, 900, 900)
	row, err := db.GetWhitelistByName("permanent")
	mustTimer(t, err)
	if row.IsActive {
		t.Fatal("partial permanent-list activation committed")
	}
	mustTimer(t, db.SetWhitelistActive("lesson", true))
	advance(time.Minute)
	mustTimer(t, db.ChangeWhitelistName("lesson", "renamed"))
	timerRow(t, db, "renamed", true, 900, 840)
	if err := db.ResetWhitelistTimers([]string{"renamed", "missing"}); err == nil {
		t.Fatal("invalid reset accepted")
	}
	timerRow(t, db, "renamed", true, 900, 840)
	mustTimer(t, db.DeleteWhitelist("renamed"))
	if len(db.timers) != 0 {
		t.Fatal("deleted timer retained in memory")
	}
	mustTimer(t, db.CreateTemporaryWhitelist("renamed", 30, false))
	timerRow(t, db, "renamed", false, 30, 30)
}

func TestTemporaryWhitelistRestartSettings(t *testing.T) {
	for _, save := range []bool{false, true} {
		t.Run(map[bool]string{false: "reset", true: "restore"}[save], func(t *testing.T) {
			path := filepath.Join(t.TempDir(), "restart.db")
			db := New(path)
			mustTimer(t, db.Connect())
			now := time.Now()
			db.now = func() time.Time { return now }
			if save {
				mustTimer(t, db.SetSetting(SaveTemporaryWhitelistState, "1"))
			}
			mustTimer(t, db.CreateTemporaryWhitelist("running", 900, false))
			mustTimer(t, db.CreateTemporaryWhitelist("paused", 600, true))
			mustTimer(t, db.SetWhitelistActive("running", true))
			remaining := int64(1200)
			mustTimer(t, db.UpdateWhitelistTimer("paused", 600, &remaining, true))
			now = now.Add(3 * time.Minute)
			mustTimer(t, db.SaveWhitelistTimerState())
			mustTimer(t, db.Disconnect())
			db = New(path)
			mustTimer(t, db.Connect())
			if save {
				row, err := db.GetWhitelistByName("running")
				mustTimer(t, err)
				if !row.IsActive || row.Temporary.RemainingSeconds < 719 || row.Temporary.RemainingSeconds > 720 {
					t.Fatalf("running state not restored: %+v %+v", row, row.Temporary)
				}
				timerRow(t, db, "paused", false, 600, 1200)
			} else {
				timerRow(t, db, "running", false, 900, 900)
				timerRow(t, db, "paused", false, 600, 600)
			}
			mustTimer(t, db.Disconnect())
			db = New(path)
			mustTimer(t, db.Connect())
			defer db.Disconnect()
			timerRow(t, db, "running", false, 900, 900)
			timerRow(t, db, "paused", false, 600, 600)
		})
	}
}

func TestTemporaryWhitelistRejectsInvalidTimerValues(t *testing.T) {
	db, _ := timerDatabase(t)
	for _, seconds := range []int64{0, -1, maxTimerSeconds + 1} {
		if err := db.CreateTemporaryWhitelist("invalid", seconds, false); err == nil {
			t.Fatalf("accepted duration %d", seconds)
		}
	}
	mustTimer(t, db.CreateTemporaryWhitelist("lesson", 900, false))
	for _, seconds := range []int64{-1, maxTimerSeconds + 1} {
		if err := db.UpdateWhitelistTimer("lesson", 900, &seconds, false); err == nil {
			t.Fatalf("accepted remaining time %d", seconds)
		}
	}
	timerRow(t, db, "lesson", false, 900, 900)
}

func TestTemporaryWhitelistExpiryDuringDisable(t *testing.T) {
	for _, autoDelete := range []bool{false, true} {
		db, advance := timerDatabase(t)
		mustTimer(t, db.CreateTemporaryWhitelist("lesson", 1, autoDelete))
		mustTimer(t, db.SetWhitelistActive("lesson", true))
		advance(time.Second)
		mustTimer(t, db.SetWhitelistActive("lesson", false))
		if autoDelete {
			if _, err := db.GetWhitelistByName("lesson"); !errors.Is(err, sql.ErrNoRows) {
				t.Fatal("disabling after deadline bypassed auto-delete")
			}
		} else {
			timerRow(t, db, "lesson", false, 1, 0)
		}
	}
}

func TestTemporaryWhitelistExplicitRemainingOverridesDurationDelta(t *testing.T) {
	db, _ := timerDatabase(t)
	mustTimer(t, db.CreateTemporaryWhitelist("lesson", 1, false))
	remaining := int64(maxTimerSeconds)
	mustTimer(t, db.UpdateWhitelistTimer("lesson", 1, &remaining, false))
	remaining = 1200
	mustTimer(t, db.UpdateWhitelistTimer("lesson", maxTimerSeconds, &remaining, false))
	timerRow(t, db, "lesson", false, float64(maxTimerSeconds), 1200)
}
