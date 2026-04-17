package sqlite

import (
	"bigbrother_server_backend/packages/domain/entity"
	dbcommon "bigbrother_server_backend/packages/infrastructure/database/common"
	"database/sql"
	"errors"
	"fmt"
	"slices"

	"github.com/abaxoth0/Ain/common"
	"github.com/google/uuid"
	_ "github.com/ncruces/go-sqlite3/driver"
)

func (db *Database) GetUserByName(username string) (*entity.User, error) {
	dbcommon.Log.Trace("Getting user with name \""+username+"\"...", nil)

	row := db.conn.QueryRow(
		"SELECT id, name, addr, whitelist_id FROM user WHERE name = ?",
		username,
	)
	if err := row.Err(); err != nil {
		return nil, err
	}

	user := entity.User{}
	var id []byte
	if err := row.Scan(&id, &user.Name, &user.Addr, &user.WhitelistID); err != nil {
		return nil, err
	}
	user.Id = string(id)

	dbcommon.Log.Trace("Getting user with name \""+username+"\": OK", nil)

	return &user, nil
}

// FIXME Code duplication
func (db *Database) GetUserByAddr(userAddr string) (*entity.User, error) {
	dbcommon.Log.Trace("Getting user with address \""+userAddr+"\"...", nil)

	row := db.conn.QueryRow(
		"SELECT id, name, addr, whitelist_id FROM user WHERE addr = ?",
		userAddr,
	)
	if err := row.Err(); err != nil {
		return nil, err
	}

	user := entity.User{}
	var id []byte
	if err := row.Scan(&id, &user.Name, &user.Addr, &user.WhitelistID); err != nil {
		return nil, err
	}
	user.Id = string(id)

	dbcommon.Log.Trace("Getting user with address \""+userAddr+"\": OK", nil)

	return &user, nil
}

func (db *Database) CreateUser(name string, addr string) (string, error) {
	dbcommon.Log.Info("Creating new user - \""+name+"\" | \""+addr+"\" ...", nil)

	id := uuid.New()

	_, err := db.conn.Exec(
		"INSERT INTO user (id, name, addr, whitelist_id) VALUES (?, ?, ?)",
		id, name, addr,
	)
	if err != nil {
		return "", err
	}

	dbcommon.Log.Info("Creating new user - \""+name+"\" | \""+addr+"\": OK", nil)

	return id.String(), nil
}

var userProperties  = []string{"name", "addr", "whitelist_id"}

func (db *Database) changeUserProperty(property string, username string, newValue any, tx *sql.Tx) error {
	var executor dbcommon.Executor = common.Ternary(tx == nil, dbcommon.Executor(db.conn), dbcommon.Executor(tx))

	dbcommon.Log.Info(fmt.Sprintf("Changing %s of user \"%s\" to \"%v\"...", property, username, newValue), nil)

	if !slices.Contains(userProperties, property) {
		return errors.New("invalid user property: " + property)
	}
	if _, err := db.GetUserByName(username); err != nil {
		return err
	}

	_, err := executor.Exec(
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
	return db.changeUserProperty("addr", username, newAddr, nil)
}

func (db *Database) ChangeUserName(username string, newUsername string) error {
	_, err := db.GetUserByName(newUsername)
	if !errors.Is(err, sql.ErrNoRows) {
		if err != nil {
			return err
		}
		return fmt.Errorf("User name \"%s\" already in use", newUsername)
	}
	return db.changeUserProperty("name", username, newUsername, nil)
}

func (db *Database) ChangeUsersWhitelist(newWhitelistName string, usernames ...string) error {
	wl, err := db.GetWhitelistByName(newWhitelistName)
	if err != nil {
		return err
	}
	transaction, err := dbcommon.NewTransaction(
		"user(-s) whitelist update",
		db.conn,
		func(tx *sql.Tx, username string) error {
			if err := db.changeUserProperty("whitelist_id", username, wl.ID, tx); err != nil {
				return err
			}
			return nil
		},
		usernames...,
	)
	if err != nil {
		return err
	}
	return transaction.Commit()
}

func (db *Database) DeleteUsers(usernames ...string) error {
	transaction, err := dbcommon.NewTransaction(
		"user(-s) delete",
		db.conn,
		func(tx *sql.Tx, username string) error {
			dbcommon.Log.Info("Deleting user \""+username+"\"...", nil)
			if _, err := tx.Exec("DELETE FROM user WHERE name = ?", username); err != nil {
				return err
			}
			dbcommon.Log.Info("Deleting user \""+username+"\": OK", nil)
			return nil
		},
		usernames...,
	)
	if err != nil {
		return err
	}
	return transaction.Commit()
}
