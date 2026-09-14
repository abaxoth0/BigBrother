//go:build windows

package main

import (
	"bigbrother_server_backend/cmd/app"
	"bigbrother_server_backend/packages/common/log"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/infrastructure/notification"
	"bigbrother_server_backend/packages/infrastructure/pending"
	"bigbrother_server_backend/packages/presentation/discovery"
	"bigbrother_server_backend/packages/presentation/rpc"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"github.com/abaxoth0/Ain/logger"
)

var mainLogger = logger.NewSource("MAIN", log.DefaultLogger)

const frontendPipePath = `\\.\pipe\BigBrother.Server.Frontend`
const frontendPipeBufSize = 65536
const defaultBackendPort = 1984
const serviceName = "BigBrother Server"
const pipeSecurityDescriptor = "D:(A;;GA;;;WD)"

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

	time.Sleep(time.Millisecond * 50)

	connManager := connection.NewMemoryResidentConnectionManager()
	dbPath := "bb-server.db"
	if exe, err := os.Executable(); err == nil {
		exeDir := filepath.Dir(exe)
		programData := filepath.Join(os.Getenv("PROGRAMDATA"), "BigBrother")
		if _, err := os.Stat(programData); err == nil {
			dbPath = filepath.Join(programData, "bb-server.db")
		} else {
			dbPath = filepath.Join(exeDir, "bb-server.db")
		}
	}
	mainLogger.Info("Database path: "+dbPath, nil)
	db := sqlite.New(dbPath)
	if err := db.Connect(); err != nil {
		mainLogger.Fatal("Database connection error", err.Error(), nil)
	}
	defer db.Disconnect()

	pendingUsersStorage := pending.NewUserStorage()
	notifMgr := notification.NewManager()

	discoveryListener := discovery.New(db)

	backendServer := rpc.NewServer(
		"Backend",
		rpc.NewBackendHandler(db, connManager, pendingUsersStorage, notifMgr),
		&rpc.ServerConfig{
			Network: rpc.NetworkTCP,
		},
	)

	frontendServer := rpc.NewServer(
		"Frontend",
		rpc.NewFrontendHandler(db, connManager, pendingUsersStorage, notifMgr),
		&rpc.ServerConfig{
			Network:            rpc.NetworkPipe,
			InputBufferSize:    frontendPipeBufSize,
			OutputBufferSize:   frontendPipeBufSize,
			SecurityDescriptor: pipeSecurityDescriptor,
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

	if isService() {
		mainLogger.Info("Starting as Windows service...", nil)
		runService(backendAddr, frontendPipePath, discoveryListener, connManager, backendServer, frontendServer)
	} else {
		mainLogger.Info("Starting in console mode...", nil)
		runConsole(backendAddr, frontendPipePath, discoveryListener, backendServer, frontendServer, connManager)
	}
}

func startAndWait(backendAddr, frontendPipePath string, discoveryListener *discovery.Listener, backendServer, frontendServer *rpc.Server, connManager *connection.MemoryResidentConnectionManager, stopCh <-chan struct{}) {
	if err := discoveryListener.Start(); err != nil {
		mainLogger.Fatal("Discovery listener error", err.Error(), nil)
	}

	go func() {
		if err := frontendServer.Start(frontendPipePath); err != nil {
			mainLogger.Fatal("Frontend RPC Handler error", err.Error(), nil)
		}
	}()

	mainLogger.Info("Backend TCP address: "+backendAddr, nil)
	go func() {
		if err := backendServer.Start(backendAddr); err != nil {
			mainLogger.Fatal("Backend RPC Handler error", err.Error(), nil)
		}
	}()

	<-stopCh
	mainLogger.Info("Shutting down servers...", nil)
	discoveryListener.Stop()
	frontendServer.Stop(0)
	backendServer.Stop(0)
	connManager.Stop()
}

func runConsole(backendAddr, frontendPipePath string, discoveryListener *discovery.Listener, backendServer, frontendServer *rpc.Server, connManager *connection.MemoryResidentConnectionManager) {
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	stopCh := make(chan struct{}, 1)
	go func() {
		<-sigCh
		close(stopCh)
	}()
	startAndWait(backendAddr, frontendPipePath, discoveryListener, backendServer, frontendServer, connManager, stopCh)
}
