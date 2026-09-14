package sqlite

import (
	dbcommon "bigbrother_server_backend/packages/infrastructure/database/common"
	"database/sql"
	"errors"
	"fmt"

	_ "github.com/ncruces/go-sqlite3/driver"
)

type Database struct {
	isConnected bool
	conn        *sql.DB
	path        string
}

func New(path string) *Database {
	return &Database{
		path: path,
	}
}

func (db *Database) Connect() error {
	dbcommon.Log.Info("Connecting...", nil)

	if db.isConnected {
		return errors.New("Already connected to the database")
	}

	conn, err := sql.Open("sqlite3", db.path)
	if err != nil {
		return err
	}

	// SQLite serializes writers; a single connection avoids "database is locked"
	// under concurrent handlers (WAL still permits parallel reads), and a busy
	// timeout keeps short lock contention from failing immediately.
	conn.SetMaxOpenConns(1)

	db.conn = conn
	db.isConnected = true

	if err := db.initTables(); err != nil {
		db.Disconnect()
		return err
	}

	if _, err := db.conn.Exec("PRAGMA journal_mode = WAL"); err != nil {
		db.Disconnect()
		return fmt.Errorf("Failed to enable WAL: %v", err)
	}
	if _, err := db.conn.Exec("PRAGMA busy_timeout = 5000"); err != nil {
		db.Disconnect()
		return fmt.Errorf("Failed to set busy timeout: %v", err)
	}

	dbcommon.Log.Info("Connecting: OK", nil)

	return nil
}

func (db *Database) Disconnect() error {
	dbcommon.Log.Info("Disconnecting...", nil)

	if !db.isConnected {
		return dbcommon.ErrNotConnectedToDB
	}
	if err := db.conn.Close(); err != nil {
		return err
	}
	db.conn = nil
	db.isConnected = false

	dbcommon.Log.Info("Disconnecting: OK", nil)

	return nil
}

func (db *Database) initTables() error {
	dbcommon.Log.Info("Initializing tables...", nil)

	if !db.isConnected {
		return dbcommon.ErrNotConnectedToDB
	}

	// By default SQLite disables foreign key constraints for backward compatability, so need to enable them
	enableForeignKeysSQL :=
		`PRAGMA foreign_keys = ON`
	createWhitelistTableSQL :=
		`CREATE TABLE IF NOT EXISTS whitelist (
		id 				BLOB NOT NULL PRIMARY KEY,
		name 			TEXT UNIQUE NOT NULL,
		parent_id		BLOB REFERENCES whitelist(id) ON DELETE SET NULL
	)`
	createWhitelistDomainTableSQL :=
		`CREATE TABLE IF NOT EXISTS whitelist_entry (
		id 				BLOB NOT NULL PRIMARY KEY,
		value 			TEXT NOT NULL,
		whitelist_id	BLOB NOT NULL REFERENCES whitelist(id) ON DELETE CASCADE
	)`
	createUserTableSQL :=
		`CREATE TABLE IF NOT EXISTS user (
		id 				BLOB NOT NULL PRIMARY KEY,
		name 			TEXT UNIQUE NOT NULL,
		addr			TEXT UNIQUE NOT NULL,
		whitelist_id	BLOB REFERENCES whitelist(id) ON DELETE SET NULL
	)`
	createSettingsTableSQL :=
		`CREATE TABLE IF NOT EXISTS settings (
		key 	TEXT NOT NULL PRIMARY KEY,
		value	TEXT NOT NULL
	)`

	queries := []string{
		enableForeignKeysSQL,
		createWhitelistTableSQL,
		createWhitelistDomainTableSQL,
		createUserTableSQL,
		createSettingsTableSQL,
	}
	for _, query := range queries {
		if _, err := db.conn.Exec(query); err != nil {
			return fmt.Errorf("Failed to initialize table: %v", err)
		}
	}

	dbcommon.Log.Info("Initializing tables: OK", nil)

	return nil
}
