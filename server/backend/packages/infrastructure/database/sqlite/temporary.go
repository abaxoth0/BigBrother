package sqlite

import (
	"bigbrother_server_backend/packages/domain/entity"
	"database/sql"
	"errors"
	"math"
	"time"

	"github.com/google/uuid"
)

const SaveTemporaryWhitelistState = "save_temporary_whitelist_state"
const maxTimerSeconds = math.MaxInt64 / int64(time.Second)

type whitelistTimer struct {
	duration   time.Duration
	remaining  time.Duration
	updated    time.Time
	active     bool
	autoDelete bool
}

func (t *whitelistTimer) left(now time.Time) time.Duration {
	remaining := t.remaining
	if t.active {
		remaining -= now.Sub(t.updated)
	}
	if remaining < 0 {
		return 0
	}
	return remaining
}

func (db *Database) applyWhitelistTimer(wl *entity.Whitelist) {
	if timer, ok := db.timers[wl.ID]; ok {
		wl.IsActive = timer.active
		wl.Temporary = &entity.WhitelistTimer{
			DurationSeconds:  int64(timer.duration / time.Second),
			RemainingSeconds: timer.left(db.now()).Seconds(),
			AutoDelete:       timer.autoDelete,
		}
	}
}

func (db *Database) loadWhitelistTimers() error {
	value, err := db.GetSetting(SaveTemporaryWhitelistState)
	if err != nil {
		return err
	}
	rows, err := db.conn.Query("SELECT whitelist_id, duration_seconds, auto_delete, saved_remaining_ns, saved_active FROM whitelist_timer")
	if err != nil {
		return err
	}
	defer rows.Close()
	db.timers = make(map[string]*whitelistTimer)
	for rows.Next() {
		var id string
		var seconds int64
		var remaining sql.NullInt64
		timer := &whitelistTimer{updated: db.now()}
		if err := rows.Scan(&id, &seconds, &timer.autoDelete, &remaining, &timer.active); err != nil {
			return err
		}
		if seconds <= 0 || seconds > maxTimerSeconds {
			return errors.New("invalid saved whitelist duration")
		}
		timer.duration = time.Duration(seconds) * time.Second
		timer.remaining = timer.duration
		if value == "1" && remaining.Valid {
			timer.remaining = time.Duration(max(0, remaining.Int64))
			timer.active = timer.active && timer.remaining > 0
		} else {
			timer.active = false
		}
		db.timers[id] = timer
	}
	if err := rows.Err(); err != nil {
		return err
	}
	rows.Close()
	_, err = db.conn.Exec("UPDATE whitelist_timer SET saved_remaining_ns = NULL, saved_active = 0")
	return err
}

func (db *Database) SaveWhitelistTimerState() error {
	db.timerMu.Lock()
	defer db.timerMu.Unlock()
	value, err := db.GetSetting(SaveTemporaryWhitelistState)
	if err != nil {
		return err
	}
	tx, err := db.conn.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	if _, err := tx.Exec("UPDATE whitelist_timer SET saved_remaining_ns = NULL, saved_active = 0"); err != nil {
		return err
	}
	if value == "1" {
		now := db.now()
		for id, timer := range db.timers {
			remaining := timer.left(now)
			if _, err := tx.Exec("UPDATE whitelist_timer SET saved_remaining_ns = ?, saved_active = ? WHERE whitelist_id = ?",
				int64(remaining), timer.active && remaining > 0, id); err != nil {
				return err
			}
		}
	}
	return tx.Commit()
}

func (db *Database) CreateTemporaryWhitelist(name string, durationSeconds int64, autoDelete bool) error {
	if durationSeconds <= 0 || durationSeconds > maxTimerSeconds {
		return errors.New("duration must be a positive number of seconds within the timer range")
	}
	db.timerMu.Lock()
	defer db.timerMu.Unlock()
	tx, err := db.conn.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	id := uuid.NewString()
	if _, err := tx.Exec("INSERT INTO whitelist (id, name) VALUES (?, ?)", id, name); err != nil {
		return err
	}
	if _, err := tx.Exec("INSERT INTO whitelist_timer (whitelist_id, duration_seconds, auto_delete) VALUES (?, ?, ?)", id, durationSeconds, autoDelete); err != nil {
		return err
	}
	if err := tx.Commit(); err != nil {
		return err
	}
	duration := time.Duration(durationSeconds) * time.Second
	db.timers[id] = &whitelistTimer{duration: duration, remaining: duration, updated: db.now(), autoDelete: autoDelete}
	return nil
}

func (db *Database) UpdateWhitelistTimer(name string, durationSeconds int64, remainingSeconds *int64, autoDelete bool) error {
	if durationSeconds <= 0 || durationSeconds > maxTimerSeconds {
		return errors.New("invalid timer duration")
	}
	if remainingSeconds != nil && (*remainingSeconds < 0 || *remainingSeconds > maxTimerSeconds) {
		return errors.New("invalid remaining time")
	}
	db.timerMu.Lock()
	defer db.timerMu.Unlock()
	var id string
	if err := db.conn.QueryRow("SELECT id FROM whitelist WHERE name = ?", name).Scan(&id); err != nil {
		return err
	}
	timer, ok := db.timers[id]
	if !ok {
		return errors.New("whitelist is not temporary")
	}
	next := *timer
	now := db.now()
	duration := time.Duration(durationSeconds) * time.Second
	remaining := timer.left(now)
	if remainingSeconds != nil {
		next.remaining = time.Duration(*remainingSeconds) * time.Second
	} else {
		delta := duration - timer.duration
		if delta > 0 && remaining > time.Duration(math.MaxInt64)-delta {
			return errors.New("remaining time exceeds timer range")
		}
		next.remaining = max(0, remaining+delta)
	}
	next.duration, next.autoDelete, next.updated = duration, autoDelete, now
	if next.remaining <= 0 {
		next.active = false
	}
	tx, err := db.conn.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	deleted := next.remaining <= 0 && next.autoDelete
	if deleted {
		if err := deleteWhitelist(tx, id); err != nil {
			return err
		}
	} else if _, err := tx.Exec("UPDATE whitelist_timer SET duration_seconds = ?, auto_delete = ? WHERE whitelist_id = ?", durationSeconds, autoDelete, id); err != nil {
		return err
	}
	if err := tx.Commit(); err != nil {
		return err
	}
	if deleted {
		delete(db.timers, id)
	} else {
		db.timers[id] = &next
	}
	return nil
}

func (db *Database) ResetWhitelistTimers(names []string) error {
	db.timerMu.Lock()
	defer db.timerMu.Unlock()
	ids := make([]string, 0, len(names))
	for _, name := range names {
		var id string
		if err := db.conn.QueryRow("SELECT id FROM whitelist WHERE name = ?", name).Scan(&id); err != nil {
			return err
		}
		if _, ok := db.timers[id]; !ok {
			return errors.New("whitelist is not temporary")
		}
		ids = append(ids, id)
	}
	now := db.now()
	for _, id := range ids {
		timer := db.timers[id]
		timer.remaining, timer.updated = timer.duration, now
	}
	return nil
}

func deleteWhitelist(tx *sql.Tx, id string) error {
	for _, query := range []string{
		"UPDATE user SET whitelist_id = NULL WHERE whitelist_id = ?",
		"UPDATE whitelist SET parent_id = NULL WHERE parent_id = ?",
		"DELETE FROM whitelist_entry WHERE whitelist_id = ?",
		"DELETE FROM whitelist_timer WHERE whitelist_id = ?",
		"DELETE FROM whitelist WHERE id = ?",
	} {
		if _, err := tx.Exec(query, id); err != nil {
			return err
		}
	}
	return nil
}

func (db *Database) ExpireWhitelistTimers() (bool, error) {
	db.timerMu.Lock()
	defer db.timerMu.Unlock()
	now := db.now()
	expired := []string{}
	for id, timer := range db.timers {
		if timer.active && timer.left(now) <= 0 {
			expired = append(expired, id)
		}
	}
	if len(expired) == 0 {
		return false, nil
	}
	tx, err := db.conn.Begin()
	if err != nil {
		return false, err
	}
	defer tx.Rollback()
	for _, id := range expired {
		if db.timers[id].autoDelete {
			if err := deleteWhitelist(tx, id); err != nil {
				return false, err
			}
		}
	}
	if err := tx.Commit(); err != nil {
		return false, err
	}
	for _, id := range expired {
		timer := db.timers[id]
		timer.active, timer.remaining, timer.updated = false, 0, now
		if timer.autoDelete {
			delete(db.timers, id)
		}
	}
	return true, nil
}
