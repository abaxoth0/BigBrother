package dbcommon

import (
	"bigbrother_server_backend/packages/common/log"
	"database/sql"

	"github.com/abaxoth0/Ain/errs"
	"github.com/abaxoth0/Ain/logger"
)

var Log = logger.NewSource("DATABASE", log.DefaultLogger)

var (
	ErrNotConnectedToDB = errs.NewStatusError("Connection to the database is not established", 500)
)

type Executor interface {
    Exec(query string, args ...any) (sql.Result, error)
}

type Transaction struct {
	name string
	tx *sql.Tx
}

func NewTransaction[T any](name string, db *sql.DB, prepFunc func(*sql.Tx, T) error, prepEntries ...T) (*Transaction, error) {
	Log.Info("Preparing \""+name+"\" transaction...", nil)
	tx, err := db.Begin()
	if err != nil {
		return nil, err
	}
	for _, entry := range prepEntries {
		if err := prepFunc(tx, entry); err != nil {
			return nil, err
		}
	}
	Log.Info("Preparing \""+name+"\" transaction: OK", nil)

	return &Transaction{
		name: name,
		tx: tx,
	}, nil
}

func (t *Transaction) Commit() error {
	Log.Info("Committing \""+t.name+"\" update transaction...", nil)
	if err := t.tx.Commit(); err != nil {
		return err
	}
	Log.Info("Committing \""+t.name+"\" transaction: OK", nil)
	return nil
}
