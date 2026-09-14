package sqlite

import (
	"bigbrother_server_backend/packages/domain/entity"
	dbcommon "bigbrother_server_backend/packages/infrastructure/database/common"
	"database/sql"
	"errors"
	"fmt"
	"slices"

	"github.com/abaxoth0/Ain/common"
	"github.com/abaxoth0/Ain/errs"
	"github.com/google/uuid"
	_ "github.com/ncruces/go-sqlite3/driver"
)

func (db *Database) GetUserByName(username string) (*entity.User, error) {
	dbcommon.Log.Trace("Getting user with name \""+username+"\"...", nil)

	row := db.conn.QueryRow(
		"SELECT id, name, addr, whitelist_id FROM user WHERE name = ?",
		username,
	)

	user := entity.User{}
	var id []byte
	var wlID sql.NullString
	if err := row.Scan(&id, &user.Name, &user.Addr, &wlID); err != nil {
		return nil, err
	}
	user.Id = string(id)
	user.WhitelistID = wlID.String

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

	user := entity.User{}
	var id []byte
	var wlID sql.NullString
	if err := row.Scan(&id, &user.Name, &user.Addr, &wlID); err != nil {
		return nil, err
	}
	user.Id = string(id)
	user.WhitelistID = wlID.String

	dbcommon.Log.Trace("Getting user with address \""+userAddr+"\": OK", nil)

	return &user, nil
}

func (db *Database) CreateUser(name string, addr string) (string, error) {
	dbcommon.Log.Info("Creating new user - \""+name+"\" | \""+addr+"\" ...", nil)

	// Check if a user with this name already exists — update addr
	if existing, err := db.GetUserByName(name); err == nil {
		dbcommon.Log.Info("User with name \""+name+"\" already exists, updating addr to \""+addr+"\"", nil)
		_, err := db.conn.Exec(
			"UPDATE user SET addr = ? WHERE id = ?",
			addr, existing.Id,
		)
		if err != nil {
			dbcommon.Log.Error("Updating existing user addr", err.Error(), nil)
			return "", err
		}
		dbcommon.Log.Info("Updating existing user addr: OK", nil)
		return existing.Id, nil
	}

	// Check if a user with this addr already exists — update name
	if existing, err := db.GetUserByAddr(addr); err == nil {
		dbcommon.Log.Info("User with addr \""+addr+"\" already exists, updating name to \""+name+"\"", nil)
		_, err := db.conn.Exec(
			"UPDATE user SET name = ? WHERE id = ?",
			name, existing.Id,
		)
		if err != nil {
			dbcommon.Log.Error("Updating existing user name", err.Error(), nil)
			return "", err
		}
		dbcommon.Log.Info("Updating existing user name: OK", nil)
		return existing.Id, nil
	}

	id := uuid.New()

	_, err := db.conn.Exec(
		"INSERT INTO user (id, name, addr) VALUES (?, ?, ?)",
		id, name, addr,
	)
	if err != nil {
		dbcommon.Log.Error("Creating new user - \""+name+"\" | \""+addr+"\"", err.Error(), nil)
		return "", err
	}

	dbcommon.Log.Info("Creating new user - \""+name+"\" | \""+addr+"\": OK", nil)

	return id.String(), nil
}

var userProperties = []string{"name", "addr", "whitelist_id"}
var uniqueUserProperties = []string{"name", "addr"}

func (db *Database) changeUserProperty(property string, username string, newValue any, tx *sql.Tx) error {
	var executor dbcommon.Executor = common.Ternary(tx == nil, dbcommon.Executor(db.conn), dbcommon.Executor(tx))

	dbcommon.Log.Info(fmt.Sprintf("Changing %s of user \"%s\" to \"%v\"...", property, username, newValue), nil)

	if !slices.Contains(userProperties, property) {
		return errors.New("invalid user property: " + property)
	}
	_, err := db.GetUserByName(username)
	if err != nil {
		return err
	}

	if slices.Contains(uniqueUserProperties, property) {
		var curUser *entity.User
		switch property {
		case "name":
			curUser, err = db.GetUserByName(newValue.(string))
		case "addr":
			curUser, err = db.GetUserByAddr(newValue.(string))
		}

		if errors.Is(err, sql.ErrNoRows) {
			goto update
		}
		if err != nil || curUser == nil {
			return err
		}
		if curUser.Name != username {
			return errs.NewStatusError(
				fmt.Sprintf("Unique constraint violation for attribute %s with value \"%v\"", property, newValue),
				errs.StatusConflict.Status(),
			)
		}
	}
update:

	_, err = executor.Exec(
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
