//go:build windows

package main

import (
	"bigbrother_server_backend/cmd/app"
	"bigbrother_server_backend/packages/common/log"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/presentation/rpc"
	"time"

	"github.com/abaxoth0/Ain/logger"
)

var mainLogger = logger.NewSource("MAIN", log.DefaultLogger)

func main() {
	log.DefaultLoggerConfig.Trace = true
	log.DefaultLoggerConfig.Debug = true

	app.StartInit()
	app.InitDefaults()
	app.EndInit()

	go func() {
		if err := log.DefaultLogger.Start(); err != nil {
			panic(err.Error())
		}
	}()
	defer func() {
		if err := log.DefaultLogger.Stop(true); err != nil {
			mainLogger.Error("Failed to stop logger", err.Error(), nil)
		}
	}()

	// Reserve some time for logger to start up
	time.Sleep(time.Millisecond * 50)

	connManger := connection.NewMemoryResidentConnectionManager()
	db := sqlite.New("bb-server.db")
	if err := db.Connect(); err != nil {
		mainLogger.Fatal("Database connection error", err.Error(), nil)
	}
	defer db.Disconnect()

	server := rpc.NewDuplexServer(db, connManger)
	handler := rpc.NewDuplexHandler(server)

	if err := handler.Start(); err != nil {
		mainLogger.Fatal("RPC Handler error", err.Error(), nil)
	}

	// if err := sqlite.Test(); err != nil {
	// 	panic(err)
	// }
	//
	// fmt.Println("TEST: OK")
}
