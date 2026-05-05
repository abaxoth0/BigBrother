//go:build windows

package main

import (
	"bigbrother_server_backend/cmd/app"
	"bigbrother_server_backend/packages/common/log"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/infrastructure/pending"
	"bigbrother_server_backend/packages/presentation/rpc"
	"time"

	"github.com/abaxoth0/Ain/logger"
)

var mainLogger = logger.NewSource("MAIN", log.DefaultLogger)

const frontendPipePath = `\\.\pipe\BigBrother.Server.Frontend`
const backendPipePath  = `\\.\pipe\BigBrother.Server.Backend`
const frontendPipeBufSize = 65536
const backendPipeBufSize  = 65536

const backendSecurityDescriptor = "D:P(A;;GA;;;AU)(A;;GA;;;SY)"

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

	connManager := connection.NewMemoryResidentConnectionManager()
	db := sqlite.New("bb-server.db")
	if err := db.Connect(); err != nil {
		mainLogger.Fatal("Database connection error", err.Error(), nil)
	}
	defer db.Disconnect()

	pendingUsersStorage := pending.NewUserStorage()

	backendServer := rpc.NewServer(
		"Backend",
		rpc.NewBackendHandler(db, connManager, pendingUsersStorage) ,
		&rpc.ServerConfig{
			InputBufferSize: 	backendPipeBufSize,
			OutputBufferSize: 	backendPipeBufSize,
			SecurityDescriptor: backendSecurityDescriptor,
		},
	)

	frontendServer := rpc.NewServer(
		"Frontend",
		rpc.NewFrontendHandler(db, connManager, pendingUsersStorage),
		&rpc.ServerConfig{
			InputBufferSize: 	frontendPipeBufSize,
			OutputBufferSize: 	frontendPipeBufSize,
		},
	)

	// TODO make this thread a hypervisor which will track status of those servers and restart them if anything

	go func() {
		if err := frontendServer.Start(frontendPipePath); err != nil {
			mainLogger.Fatal("Frontend RPC Handler error", err.Error(), nil)
		}
	}()

	if err := backendServer.Start(backendPipePath); err != nil {
		mainLogger.Fatal("Backend RPC Handler error", err.Error(), nil)
	}
}
