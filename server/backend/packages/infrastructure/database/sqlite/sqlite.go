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

	db.conn = conn
	db.isConnected = true

	if err := db.initTables(); err != nil {
		db.Disconnect()
		return err
	}

	// WAL allows concurrent readers while a writer holds the write lock, and
	// busy_timeout keeps short lock contention from failing immediately.
	// NOTE: do NOT pin MaxOpenConns(1) here — changeUserProperty reads through
	// db.conn while a *sql.Tx is open, which would deadlock on a single conn.
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
		whitelist_id	BLOB REFERENCES whitelist(id) ON DELETE SET NULL,
		token			TEXT,
		sign_public		TEXT
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

	if err := db.migrate(); err != nil {
		return err
	}

	dbcommon.Log.Info("Initializing tables: OK", nil)

	return nil
}

// migrate applies additive schema changes to databases created by older
// versions (CREATE TABLE IF NOT EXISTS only covers new databases).
func (db *Database) migrate() error {
	type column struct {
		cid     int
		name    string
		typ     string
		notNull int
		dflt    sql.NullString
		pk      int
	}

	ensureColumn := func(table, col, ddl string) error {
		rows, err := db.conn.Query("PRAGMA table_info(" + table + ")")
		if err != nil {
			return err
		}
		found := false
		for rows.Next() {
			var c column
			if err := rows.Scan(&c.cid, &c.name, &c.typ, &c.notNull, &c.dflt, &c.pk); err != nil {
				rows.Close()
				return err
			}
			if c.name == col {
				found = true
				break
			}
		}
		rows.Close()
		if err := rows.Err(); err != nil {
			return err
		}
		if found {
			return nil
		}
		_, err = db.conn.Exec(ddl)
		return err
	}

	if err := ensureColumn("user", "token", "ALTER TABLE user ADD COLUMN token TEXT"); err != nil {
		return fmt.Errorf("Failed to migrate user table: %v", err)
	}
	if err := ensureColumn("user", "sign_public", "ALTER TABLE user ADD COLUMN sign_public TEXT"); err != nil {
		return fmt.Errorf("Failed to migrate user table: %v", err)
	}
	return nil
}
