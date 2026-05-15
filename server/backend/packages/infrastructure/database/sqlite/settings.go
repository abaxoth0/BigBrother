package sqlite

import (
	dbcommon "bigbrother_server_backend/packages/infrastructure/database/common"
	"database/sql"
)

func (db *Database) GetSetting(key string) (string, error) {
	dbcommon.Log.Trace("Getting setting \""+key+"\"...", nil)

	row := db.conn.QueryRow("SELECT value FROM settings WHERE key = ?", key)
	var value string
	if err := row.Scan(&value); err != nil {
		if err == sql.ErrNoRows {
			return "", nil
		}
		return "", err
	}

	dbcommon.Log.Trace("Getting setting \""+key+"\": OK", nil)

	return value, nil
}

func (db *Database) SetSetting(key, value string) error {
	dbcommon.Log.Trace("Setting \""+key+"\" to \""+value+"\"...", nil)

	_, err := db.conn.Exec(
		"INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = ?",
		key, value, value,
	)
	if err != nil {
		return err
	}

	dbcommon.Log.Trace("Setting \""+key+"\" to \""+value+"\": OK", nil)

	return nil
}
