package sqlite

import (
	"bigbrother_server_backend/packages/db/common"
	"time"

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
	LastSeenAt 	time.Time
}

func (db *Database) GetUserByName(username string) (*User, error) {
	dbcommon.Log.Trace("Getting user with name \""+username+"\"...", nil)

	row := db.conn.QueryRow(`SELECT id, name, addr, last_seen_at, whitelist_id
		FROM user
		WHERE name = ?`,
		username,
	)
	if err := row.Err(); err != nil {
		return nil, err
	}

	user := User{}
	var id []byte
	if err := row.Scan(&id, &user.Name, &user.Addr, &user.State, &user.LastSeenAt); err != nil {
		return nil, err
	}
	user.Id = string(id)

	dbcommon.Log.Trace("Getting user with name \""+username+"\": OK", nil)

	return &user, nil
}

// FIXME Code duplication
func (db *Database) GetUserByAddr(userAddr string) (*User, error) {
	dbcommon.Log.Trace("Getting user with address \""+userAddr+"\"...", nil)

	row := db.conn.QueryRow(`SELECT id, name, addr, last_seen_at, whitelist_id
		FROM user
		WHERE addr = ?`,
		userAddr,
	)
	if err := row.Err(); err != nil {
		return nil, err
	}

	user := User{}
	var id []byte
	if err := row.Scan(&id, &user.Name, &user.Addr, &user.State, &user.LastSeenAt); err != nil {
		return nil, err
	}
	user.Id = string(id)

	dbcommon.Log.Trace("Getting user with address \""+userAddr+"\": OK", nil)

	return &user, nil
}

func (db *Database) CreateUser(name string, addr string, whitelistID string) error {
	dbcommon.Log.Info("Creating new user - \""+name+"\" | \""+addr+"\" ...", nil)

	createUserSQL := `INSERT INTO user (id, name, addr, whitelist_id)
	VALUES (?, ?, ?, ?, ?)`

	_, err := db.conn.Exec(createUserSQL, uuid.New(), name, addr, whitelistID)
	if err != nil {
		return err
	}

	dbcommon.Log.Info("Creating new user - \""+name+"\" | \""+addr+"\": OK", nil)

	return nil
}

func (db *Database) ChangeUserAddr(username string, newAddr string) error {
	user, err := db.GetUserByName(username)
	if err != nil {
		return err
	}

	dbcommon.Log.Info("Changing address of user \""+username+"\" [\""+newAddr+"\" -> \""+user.Addr+"\"]...", nil)

	changeUserAddSQL := `ALTER user SET addr = ? WHERE name = ?`

	_, err = db.conn.Exec(changeUserAddSQL, newAddr, username)
	if err != nil {
		return err
	}

	dbcommon.Log.Info("Changing address of user \""+username+"\" [\""+newAddr+"\" -> \""+user.Addr+"\"]: OK", nil)

	return nil
}
