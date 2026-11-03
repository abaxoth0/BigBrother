package rpc

import (
	Log "bigbrother_server_backend/packages/common/log"
	"crypto/rand"
	"encoding/hex"

	"github.com/abaxoth0/Ain/logger"
)

var log = logger.NewSource("RPC", Log.DefaultLogger)

// issueToken returns a random hex token used to authenticate a client.
func issueToken() string {
	b := make([]byte, 24)
	if _, err := rand.Read(b); err != nil {
		return ""
	}
	return hex.EncodeToString(b)
}
