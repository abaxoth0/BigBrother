package dbcommon

import (
	"bigbrother_server_backend/packages/common/log"

	"github.com/abaxoth0/Ain/errs"
	"github.com/abaxoth0/Ain/logger"
)

var Log = logger.NewSource("DATABASE", log.DefaultLogger)

var (
	ErrNotConnectedToDB = errs.NewStatusError("Connection to the database is not established", 500)
)
