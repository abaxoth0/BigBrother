package sqlite

import (
	"bigbrother_server_backend/packages/domain/entity"
	dbcommon "bigbrother_server_backend/packages/infrastructure/database/common"
	"database/sql"
	"errors"
	"fmt"
	"slices"
	"strings"

	"github.com/abaxoth0/Ain/common"
	"github.com/google/uuid"
	_ "github.com/ncruces/go-sqlite3/driver"
)

func (db *Database) CreateWhitelist(name string, parentID string) error {
	parentID = strings.Trim(parentID, " \n\r")

	_, err := db.conn.Exec(
		"INSERT INTO whitelist (id, name, parent_id) VALUES (?, ?, ?)",
		uuid.New(), name, common.Ternary(parentID == "", nil, any(parentID)),
	)
	if err != nil {
		return err
	}

	return nil
}

func (db *Database) GetWhitelists() ([]*entity.Whitelist, error) {
	dbcommon.Log.Trace("Getting whitelists...", nil)

	rows, err := db.conn.Query(`SELECT id, name, parent_id FROM whitelist`)
	if err != nil {
		return nil, err
	}

	wls := []*entity.Whitelist{}
	for rows.Next() {
		wl := new(entity.Whitelist)
		var parentID sql.NullString
		if err := rows.Scan(&wl.ID, &wl.Name, &parentID); err != nil {
			return nil, err
		}
		if parentID.Valid {
			wl.ParentID = parentID.String
		}
		wls = append(wls, wl)
	}
	if err = rows.Err(); err != nil {
		return nil, err
	}

	dbcommon.Log.Trace("Getting whitelists: OK", nil)

	return wls, nil
}

var whitelistProperties = []string{"id", "name"}

func (db *Database) getWhitelistBy(property string, value string) (*entity.Whitelist, error) {
	dbcommon.Log.Trace("Getting whitelist \""+value+"\"...", nil)

	if !slices.Contains(whitelistProperties, property) {
		return nil, errors.New("invalid whitelist property: " + property)
	}

	row := db.conn.QueryRow("SELECT id, name, parent_id FROM whitelist WHERE "+property+" = ?", value)
	if err := row.Err(); err != nil {
		return nil, err
	}

	wl := new(entity.Whitelist)
	var parentID sql.NullString

	if err := row.Scan(&wl.ID, &wl.Name, &parentID); err != nil {
		return nil, err
	}
	if parentID.Valid {
		wl.ParentID = parentID.String
	}

	dbcommon.Log.Trace("Getting whitelist \""+value+"\": OK", nil)

	return wl, nil
}

func (db *Database) GetWhitelistByName(name string) (*entity.Whitelist, error) {
	return db.getWhitelistBy("name", name)
}

func (db *Database) GetWhitelistByID(id string) (*entity.Whitelist, error) {
	return db.getWhitelistBy("id", id)
}

func (db *Database) GetWhitelistID(whitelistName string) (string, error) {
	dbcommon.Log.Trace("Getting ID of whitelist \""+whitelistName+"\"...", nil)

	row := db.conn.QueryRow("SELECT id FROM whitelist WHERE name = ?", whitelistName)
	if err := row.Err(); err != nil {
		return "", err
	}

	var id string
	if err := row.Scan(&id); err != nil {
		return "", err
	}

	dbcommon.Log.Trace("Getting ID of whitelist \""+whitelistName+"\": OK", nil)

	return id, nil
}

func (db *Database) getWhitelistEntryByValue(value string, whitelistID string) (string, error) {
	dbcommon.Log.Trace("Getting entry of whitelist \""+whitelistID+"\" with value \""+value+"\"...", nil)

	row := db.conn.QueryRow(
		"SELECT id FROM whitelist_entry WHERE value = ? AND whitelist_id = ?",
		value, whitelistID,
	)
	if err := row.Err(); err != nil {
		return "", err
	}

	var id string
	if err := row.Scan(&id); err != nil {
		return "", err
	}

	dbcommon.Log.Trace("Getting entry of whitelist \""+whitelistID+"\" with value \""+value+"\": OK", nil)

	return id, nil
}

func (db *Database) WhitelistEntryExists(value string, whitelistID string) (bool, error) {
	_, err := db.getWhitelistEntryByValue(value, whitelistID)
	if !errors.Is(err, sql.ErrNoRows) {
		if err != nil {
			return false, err
		}
		return true, nil
	}
	return false, nil
}

func (db *Database) ChangeWhitelistName(name string, newName string) error {
	if _, err := db.GetWhitelistByName(name); err != nil {
		return err
	}

	dbcommon.Log.Info("Chaning whitelist name [\""+name+"\" -> \""+newName+"\"]...", nil)

	if _, err := db.conn.Exec("UPDATE whitelist SET name = ? WHERE name = ?", newName, name); err != nil {
		return err
	}

	dbcommon.Log.Info("Chaning whitelist name [\""+name+"\" -> \""+newName+"\"]: OK", nil)

	return nil
}

func (db *Database) DeleteWhitelist(name string) error {
	if _, err := db.GetWhitelistByName(name); err != nil {
		return err
	}

	dbcommon.Log.Info("Deleting whitelist \""+name+"\"...", nil)

	if _, err := db.conn.Exec("DELETE FROM whitelist WHERE name = ?", name); err != nil {
		return err
	}

	dbcommon.Log.Info("Deleting whitelist \""+name+"\": OK", nil)

	return nil
}

func (db *Database) GetWhitelistEntries(name string) ([]*entity.WhitelistEntry, error) {
	dbcommon.Log.Trace("Getting entries of whitelist \""+name+"\"...", nil)

	wl, err := db.GetWhitelistByName(name)
	if err != nil {
		return nil, err
	}

	rows, err := db.conn.Query(
		`SELECT id, value FROM whitelist_entry WHERE whitelist_id = ?
		UNION ALL
		SELECT id, value FROM whitelist_entry WHERE whitelist_id = ?`,
		wl.ID, wl.ParentID,
	)
	if err != nil {
		return nil, err
	}

	entries := []*entity.WhitelistEntry{}
	for rows.Next() {
		entry := new(entity.WhitelistEntry)
		if err := rows.Scan(&entry.ID, &entry.Value); err != nil {
			return nil, err
		}
		entries = append(entries, entry)
	}
	if err = rows.Err(); err != nil {
		return nil, err
	}

	dbcommon.Log.Trace("Getting entries of whitelist \""+name+"\": OK", nil)

	return entries, nil
}

func (db *Database) AddWhitelistEntry(value string, wlName string) error {
	wlID, err := db.GetWhitelistID(wlName)
	if err != nil {
		return err
	}
	ok, err := db.WhitelistEntryExists(value, wlID)
	if err != nil {
		return err
	}
	if ok {
		return fmt.Errorf("Whitelist \"%s\" already have entry with value \"%s\"", wlName, value)
	}

	dbcommon.Log.Trace("Adding entry \""+value+"\" in whitelist \""+wlName+"\"...", nil)

	_, err = db.conn.Exec(
		"INSERT INTO whitelist_entry (id, value, whitelist_id) VALUES (?, ?, ?)",
		uuid.New(), value, wlID,
	)
	if err != nil {
		return err
	}

	dbcommon.Log.Trace("Adding entry \""+value+"\" in whitelist \""+wlName+"\": OK", nil)

	return nil
}

func (db *Database) UpdateWhitelistEntry(value string, newValue string, wlName string) error {
	wlID, err := db.GetWhitelistID(wlName)
	if err != nil {
		return err
	}
	ok, err := db.WhitelistEntryExists(value, wlID)
	if err != nil {
		return err
	}
	if !ok {
		return fmt.Errorf("Entry with value \"%s\" wasn't found in whitelist \"%s\"", value, wlName)
	}

	dbcommon.Log.Info("Changing entry \""+value+"\" in whitelist \""+wlName+"\" to \""+newValue+"\"...", nil)

	_, err = db.conn.Exec(
		"UPDATE whitelist_entry SET value = ? WHERE value = ? AND whitelist_id = ?",
		newValue, value, wlID,
	)
	if err != nil {
		return err
	}

	dbcommon.Log.Info("Changing entry \""+value+"\" in whitelist \""+wlName+"\" to \""+newValue+"\": OK", nil)

	return nil
}

func (db *Database) DeleteWhitelistEntry(value string, wlName string) error {
	wlID, err := db.GetWhitelistID(wlName)
	if err != nil {
		return err
	}
	ok, err := db.WhitelistEntryExists(value, wlID)
	if err != nil {
		return err
	}
	if !ok {
		return fmt.Errorf("Entry with value \"%s\" wasn't found in whitelist \"%s\"", value, wlName)
	}

	dbcommon.Log.Info("Deleting entry \""+value+"\" from whitelist \""+wlName+"\"...", nil)

	_, err = db.conn.Exec(
		"DELETE FROM whitelist_entry WHERE value = ? AND whitelist_id = ?",
		value, wlID,
	)
	if err != nil {
		return err
	}

	dbcommon.Log.Info("Deleting entry \""+value+"\" from whitelist \""+wlName+"\": OK", nil)

	return nil
}

// ReplaceWhitelistEntries replaces the entire entry set of a whitelist in a single
// transaction (delete-all + insert-all), so a failed save cannot leave a partial set.
func (db *Database) ReplaceWhitelistEntries(entries []string, wlName string) error {
	wlID, err := db.GetWhitelistID(wlName)
	if err != nil {
		return err
	}

	dbcommon.Log.Trace("Replacing entries of whitelist \""+wlName+"\"...", nil)

	tx, err := db.conn.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()

	if _, err := tx.Exec("DELETE FROM whitelist_entry WHERE whitelist_id = ?", wlID); err != nil {
		return err
	}

	for _, value := range entries {
		if _, err := tx.Exec(
			"INSERT INTO whitelist_entry (id, value, whitelist_id) VALUES (?, ?, ?)",
			uuid.New(), value, wlID,
		); err != nil {
			return err
		}
	}

	if err := tx.Commit(); err != nil {
		return err
	}

	dbcommon.Log.Trace("Replacing entries of whitelist \""+wlName+"\": OK", nil)

	return nil
}
