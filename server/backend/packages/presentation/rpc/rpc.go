package rpc

import (
	Log "bigbrother_server_backend/packages/common/log"
	"bigbrother_server_backend/packages/domain/entity"

	"github.com/abaxoth0/Ain/logger"
)

var log = logger.NewSource("RPC", Log.DefaultLogger)

var pendingUsers = make(map[string]*entity.PendingUser)
