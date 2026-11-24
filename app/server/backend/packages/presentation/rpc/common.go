package rpc

import (
	"fmt"
	"strings"

	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database"
)

// deleteUsers removes the users from the database and drops any live
// connections they hold, surfacing any failures as a single error.
func deleteUsers(db database.DBInstance, connManager connection.Manager, usernames ...string) error {
	if err := db.DeleteUsers(usernames...); err != nil {
		return err
	}
	errMsgs := make([]string, 0, len(usernames))
	for _, username := range usernames {
		if err := connManager.DeleteConnection(username); err != nil {
			errMsgs = append(errMsgs, err.Error())
		}
	}
	if len(errMsgs) != 0 {
		var sb strings.Builder
		fmt.Fprintf(&sb, "Failed to delete %d connection(-s):\n", len(errMsgs))
		for i, errMsg := range errMsgs {
			fmt.Fprintf(&sb, "%d %s\n", i+1, errMsg)
		}
		return fmt.Errorf("%s", sb.String())
	}
	return nil
}
