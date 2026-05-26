//go:build windows

package main

import (
	"bigbrother_server_backend/cmd/app"
	"bigbrother_server_backend/packages/common/log"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/infrastructure/pending"
	"bigbrother_server_backend/packages/presentation/discovery"
	"bigbrother_server_backend/packages/presentation/rpc"
	"fmt"
	"os"
	"time"

	"github.com/abaxoth0/Ain/logger"
)

var mainLogger = logger.NewSource("MAIN", log.DefaultLogger)

const frontendPipePath = `\\.\pipe\BigBrother.Server.Frontend`
const frontendPipeBufSize = 65536

const defaultBackendPort = 1984

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

	discoveryListener := discovery.New(db)
	if err := discoveryListener.Start(); err != nil {
		mainLogger.Fatal("Discovery listener error", err.Error(), nil)
	}
	defer discoveryListener.Stop()

	backendServer := rpc.NewServer(
		"Backend",
		rpc.NewBackendHandler(db, connManager, pendingUsersStorage),
		&rpc.ServerConfig{
			Network: rpc.NetworkTCP,
		},
	)

	frontendServer := rpc.NewServer(
		"Frontend",
		rpc.NewFrontendHandler(db, connManager, pendingUsersStorage),
		&rpc.ServerConfig{
			Network:          rpc.NetworkPipe,
			InputBufferSize:  frontendPipeBufSize,
			OutputBufferSize: frontendPipeBufSize,
		},
	)

	// Determine backend port: CLI --port flag overrides DB, DB overrides default
	backendPort := defaultBackendPort
	cliPortSet := false
	for i, arg := range os.Args {
		if arg == "--port" && i+1 < len(os.Args) {
			if _, err := fmt.Sscanf(os.Args[i+1], "%d", &backendPort); err == nil && backendPort > 0 && backendPort < 65536 {
				cliPortSet = true
				break
			}
		}
	}
	if !cliPortSet {
		if dbPort, err := db.GetSetting("server_port"); err == nil && dbPort != "" {
			if p, e := fmt.Sscanf(dbPort, "%d", &backendPort); e == nil && p == 1 && backendPort > 0 && backendPort < 65536 {
				// use db port
			}
		}
	}
	backendAddr := fmt.Sprintf(":%d", backendPort)

	// TODO make this thread a hypervisor which will track status of those servers and restart them if anything

	go func() {
		if err := frontendServer.Start(frontendPipePath); err != nil {
			mainLogger.Fatal("Frontend RPC Handler error", err.Error(), nil)
		}
	}()

	mainLogger.Info("Backend TCP address: "+backendAddr, nil)
	if err := backendServer.Start(backendAddr); err != nil {
		mainLogger.Fatal("Backend RPC Handler error", err.Error(), nil)
	}
}
