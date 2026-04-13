package sqlite

import (
	"bigbrother_server_backend/packages/db/common"
	"database/sql"
	"errors"
	"fmt"

	"github.com/google/uuid"
	_ "github.com/ncruces/go-sqlite3/driver"
)

type Database struct {
	isConnected bool
	conn 		*sql.DB
	path		string
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

	dbcommon.Log.Info("Connecting: OK", nil)

	return nil
}

func (db *Database) Disconnect() error {
	dbcommon.Log.Info("Disconnecting...", nil)

	if (!db.isConnected) {
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

	if (!db.isConnected) {
		return dbcommon.ErrNotConnectedToDB
	}

	createWhitelistTableSQL :=
	`CREATE TABLE IF NOT EXISTS whitelist (
		id 				BLOB NOT NULL PRIMARY KEY,
		name 			TEXT UNIQUE NOT NULL,
		updated_at		INTEGER NOT NULL,
		parent_id		BLOB NOT NULL REFERENCES whitelist(id)
	)`
	createWhitelistDomainTableSQL :=
	`CREATE TABLE IF NOT EXISTS whitelist_entry (
		id 				BLOB NOT NULL PRIMARY KEY,
		value 			TEXT UNIQUE NOT NULL,
		whitelist_id	BLOB NOT NULL REFERENCES whitelist(id)
	)`
	createUserTableSQL :=
	`CREATE TABLE IF NOT EXISTS user (
		id 				BLOB NOT NULL PRIMARY KEY,
		name 			TEXT UNIQUE NOT NULL,
		addr			TEXT UNIQUE NOT NULL,
		last_seen_at	INTEGER,
		whitelist_id	BLOB REFERENCES whitelist(id)
	)`

	queries := []string{
		createWhitelistTableSQL,
		createWhitelistDomainTableSQL,
		createUserTableSQL,
	}
	for _, query := range queries {
		if _, err := db.conn.Exec(query); err != nil {
			return fmt.Errorf("Failed to initialize table: %v", err)
		}
	}

	dbcommon.Log.Info("Initializing tables: OK", nil)

	return nil
}

func Test() error {
	db := New("./bb-server.db")
	if err := db.Connect(); err != nil {
		return err
	}
	defer db.Disconnect()

	_, err := db.conn.Exec(`INSERT INTO test (id, name) VALUES (?, ?)`, uuid.New(), "Obabo")
	if err != nil {
		return err
	}

	rows, err := db.conn.Query(`SELECT id, name FROM test`)
	if err != nil {
		return err
	}
	defer rows.Close()

	for rows.Next() {
		var id []byte
		var name string
		err := rows.Scan(&id, &name)
		if err != nil {
			return err
		}
		fmt.Println("DATA: ", string(id), name)
	}
	if err = rows.Err(); err != nil {
		return err
	}

	return nil
}
