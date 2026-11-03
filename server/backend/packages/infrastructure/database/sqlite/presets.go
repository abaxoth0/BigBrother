package sqlite

import (
	"bigbrother_server_backend/packages/domain/entity"
	"database/sql"
	"errors"
	"strings"
)

func (db *Database) GetWhitelistPresets() ([]entity.WhitelistPreset, error) {
	rows, err := db.conn.Query(`SELECT p.name, w.name FROM whitelist_preset p
        LEFT JOIN whitelist_preset_member m ON m.preset_name = p.name
        LEFT JOIN whitelist w ON w.id = m.whitelist_id
        ORDER BY p.name COLLATE NOCASE, p.name, w.name COLLATE NOCASE, w.name`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	presets := []entity.WhitelistPreset{}
	for rows.Next() {
		var name string
		var whitelist sql.NullString
		if err := rows.Scan(&name, &whitelist); err != nil {
			return nil, err
		}
		if len(presets) == 0 || presets[len(presets)-1].Name != name {
			presets = append(presets, entity.WhitelistPreset{Name: name, ActiveWhitelists: []string{}})
		}
		if whitelist.Valid {
			last := &presets[len(presets)-1]
			last.ActiveWhitelists = append(last.ActiveWhitelists, whitelist.String)
		}
	}
	return presets, rows.Err()
}

func (db *Database) SaveWhitelistPreset(name string, activeWhitelists []string, replace bool) error {
	if strings.TrimSpace(name) == "" || strings.ContainsAny(name, "\r\n\x00") {
		return errors.New("invalid preset name")
	}
	db.timerMu.Lock()
	defer db.timerMu.Unlock()
	tx, err := db.conn.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	query := "INSERT INTO whitelist_preset (name) VALUES (?)"
	if replace {
		query += " ON CONFLICT(name) DO NOTHING"
	}
	if _, err := tx.Exec(query, name); err != nil {
		return err
	}
	if _, err := tx.Exec("DELETE FROM whitelist_preset_member WHERE preset_name = ?", name); err != nil {
		return err
	}
	for _, whitelist := range activeWhitelists {
		var id string
		if err := tx.QueryRow("SELECT id FROM whitelist WHERE name = ?", whitelist).Scan(&id); err != nil {
			return err
		}
		if _, err := tx.Exec("INSERT INTO whitelist_preset_member (preset_name, whitelist_id) VALUES (?, ?)", name, id); err != nil {
			return err
		}
	}
	return tx.Commit()
}

func (db *Database) DeleteWhitelistPreset(name string) error {
	db.timerMu.Lock()
	defer db.timerMu.Unlock()
	tx, err := db.conn.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	if _, err := tx.Exec("DELETE FROM whitelist_preset_member WHERE preset_name = ?", name); err != nil {
		return err
	}
	if _, err := tx.Exec("DELETE FROM whitelist_preset WHERE name = ?", name); err != nil {
		return err
	}
	return tx.Commit()
}

func (db *Database) LoadWhitelistPreset(name string) (bool, error) {
	return db.changeWhitelistActivation(func(tx *sql.Tx) (map[string]bool, error) {
		var preset string
		if err := tx.QueryRow("SELECT name FROM whitelist_preset WHERE name = ?", name).Scan(&preset); err != nil {
			return nil, err
		}
		rows, err := tx.Query(`SELECT w.name, m.whitelist_id IS NOT NULL FROM whitelist w
            LEFT JOIN whitelist_preset_member m ON m.whitelist_id = w.id AND m.preset_name = ?`, name)
		if err != nil {
			return nil, err
		}
		defer rows.Close()
		states := map[string]bool{}
		for rows.Next() {
			var whitelist string
			var active bool
			if err := rows.Scan(&whitelist, &active); err != nil {
				return nil, err
			}
			states[whitelist] = active
		}
		return states, rows.Err()
	})
}
