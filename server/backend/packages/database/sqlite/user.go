package sqlite

import (
	"bigbrother_server_backend/packages/database/common"
	"database/sql"
	"errors"
	"fmt"
	"slices"

	"github.com/google/uuid"
	_ "github.com/ncruces/go-sqlite3/driver"
)

type UserState uint8

const (
	Disconnected UserState = iota
	Connected
)

type User struct {
	Id 			string
	Name 		string
	Addr 		string
	State 		UserState
	WhitelistID string
}

func (db *Database) GetUserByName(username string) (*User, error) {
	dbcommon.Log.Trace("Getting user with name \""+username+"\"...", nil)

	row := db.conn.QueryRow(
		"SELECT id, name, addr, whitelist_id FROM user WHERE name = ?",
		username,
	)
	if err := row.Err(); err != nil {
		return nil, err
	}

	user := User{}
	var id []byte
	if err := row.Scan(&id, &user.Name, &user.Addr, &user.WhitelistID); err != nil {
		return nil, err
	}
	user.Id = string(id)

	dbcommon.Log.Trace("Getting user with name \""+username+"\": OK", nil)

	return &user, nil
}

// FIXME Code duplication
func (db *Database) GetUserByAddr(userAddr string) (*User, error) {
	dbcommon.Log.Trace("Getting user with address \""+userAddr+"\"...", nil)

	row := db.conn.QueryRow(
		"SELECT id, name, addr, whitelist_id FROM user WHERE addr = ?",
		userAddr,
	)
	if err := row.Err(); err != nil {
		return nil, err
	}

	user := User{}
	var id []byte
	if err := row.Scan(&id, &user.Name, &user.Addr, &user.WhitelistID); err != nil {
		return nil, err
	}
	user.Id = string(id)

	dbcommon.Log.Trace("Getting user with address \""+userAddr+"\": OK", nil)

	return &user, nil
}

func (db *Database) CreateUser(name string, addr string, whitelistID string) error {
	dbcommon.Log.Info("Creating new user - \""+name+"\" | \""+addr+"\" ...", nil)

	_, err := db.conn.Exec(
		"INSERT INTO user (id, name, addr, whitelist_id) VALUES (?, ?, ?, ?)",
		uuid.New(), name, addr, whitelistID,
	)
	if err != nil {
		return err
	}

	dbcommon.Log.Info("Creating new user - \""+name+"\" | \""+addr+"\": OK", nil)

	return nil
}

var userProperties  = []string{"name", "addr", "whitelist_id"}

func (db *Database) changeUserProperty(property string, username string, newValue any) error {
	dbcommon.Log.Info(fmt.Sprintf("Changing %s of user \"%s\" to \"%v\"...", property, username, newValue), nil)

	if !slices.Contains(userProperties, property) {
		return errors.New("invalid user property: " + property)
	}
	if _, err := db.GetUserByName(username); err != nil {
		return err
	}

	_, err := db.conn.Exec(
		"UPDATE user SET "+property+" = ? WHERE name = ?",
		newValue, username,
	)
	if err != nil {
		return err
	}

	dbcommon.Log.Info(fmt.Sprintf("Changing %s of user \"%s\" to \"%v\": OK", property, username, newValue), nil)

	return nil
}

func (db *Database) ChangeUserAddr(username string, newAddr string) error {
	return db.changeUserProperty("addr", username, newAddr)
}

func (db *Database) ChangeUserName(username string, newUsername string) error {
	_, err := db.GetUserByName(newUsername)
	if !errors.Is(err, sql.ErrNoRows) {
		if err != nil {
			return err
		}
		return fmt.Errorf("User name \"%s\" already in use", newUsername)
	}
	return db.changeUserProperty("name", username, newUsername)
}

func (db *Database) ChangeUserWhitelist(username string, newWhitelistID string) error {
	return db.changeUserProperty("whitelist_id", username, newWhitelistID)
}

func (db *Database) DeleteUser(username string) error {
	if _, err := db.GetUserByName(username); err != nil {
		return err
	}

	dbcommon.Log.Info("Deleting user \""+username+"\"...", nil)
	if _, err := db.conn.Exec("DELETE FROM user WHERE name = ?", username); err != nil {
		return err
	}
	dbcommon.Log.Info("Deleting user \""+username+"\": OK", nil)

	return nil
}
