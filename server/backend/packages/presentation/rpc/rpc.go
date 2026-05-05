package rpc

import (
	Log "bigbrother_server_backend/packages/common/log"
	"bigbrother_server_backend/packages/domain/entity"
	"fmt"
	"net"
	"strconv"

	"github.com/abaxoth0/Ain/logger"
)

var log = logger.NewSource("RPC", Log.DefaultLogger)

var pendingUsers = make(map[string]*entity.PendingUser)

type errorType int

const (
	internalError errorType = iota
	requestError
)

func write(conn net.Conn, format string, a ...any) error {
	addrStr := conn.RemoteAddr().String()
	log.Trace("Writing to \""+addrStr+"\": \""+fmt.Sprintf(format, a...)+"\"...", nil)
	if _, err := fmt.Fprintf(conn, format, a...); err != nil {
		log.Error("Connection write error", err.Error(), nil)
		return err
	}
	log.Trace(fmt.Sprintf("Writing to \"%s\": OK", addrStr), nil)
	return nil
}

func writeln(conn net.Conn, buf ...any) error {
	addrStr := conn.RemoteAddr().String()
	log.Trace("Writing to \""+addrStr+"\": \""+fmt.Sprintln(buf...)+"\"...", nil)
	if _, err := fmt.Fprintln(conn, buf...); err != nil {
		log.Error("Connection write error", err.Error(), nil)
		return err
	}
	log.Trace(fmt.Sprintf("Writing to \"%s\": OK", addrStr), nil)
	return nil
}

func writeError(conn net.Conn, _type errorType, msg string) {
	var logMsg string
	switch _type {
	case internalError:
		logMsg = "Internal Error"
	case requestError:
		logMsg = "Request Error"
	default:
		panic("unknown error type: " + strconv.Itoa(int(_type)))
	}
	log.Error(logMsg, msg, nil)
	write(conn, "ERROR: "+msg+"\n")
}
