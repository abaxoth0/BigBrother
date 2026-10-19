//go:build windows

//go:generate make -C .. resources

package main

import (
	"bigbrother_server_backend/cmd/app"
	settingsapplication "bigbrother_server_backend/packages/application/settings"
	"bigbrother_server_backend/packages/common/log"
	"bigbrother_server_backend/packages/domain/entity"
	"bigbrother_server_backend/packages/infrastructure/connection"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"bigbrother_server_backend/packages/infrastructure/firewall"
	"bigbrother_server_backend/packages/infrastructure/notification"
	"bigbrother_server_backend/packages/infrastructure/pending"
	"bigbrother_server_backend/packages/infrastructure/secretfs"
	"bigbrother_server_backend/packages/infrastructure/tlsutil"
	"bigbrother_server_backend/packages/presentation/discovery"
	"bigbrother_server_backend/packages/presentation/rpc"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"syscall"
	"time"

	"github.com/abaxoth0/Ain/logger"
)

var productVersion = "development"

var mainLogger = logger.NewSource("MAIN", log.DefaultLogger)

const frontendPipePath = `\\.\pipe\BigBrother.Server.Frontend`
const frontendPipeBufSize = 65536
const defaultBackendPort = 1984
const serviceName = "BigBrother Server"

// Restrict the frontend pipe to SYSTEM, Builtin Administrators and the object
// owner. Previously everyone (WD) could drive approve/reject/user/whitelist
// administration from any local process.
const pipeSecurityDescriptor = "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)"

func main() {
	if len(os.Args) == 2 && os.Args[1] == "--version" {
		fmt.Println("BigBrother Server Daemon " + productVersion)
		return
	}
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
	mainLogger.Info("Version: "+productVersion, nil)

	desktop := notification.NewDesktopNotifier(notification.DeliverWindowsNotification, func(err error) {
		mainLogger.Error("Windows notification failed", err.Error(), nil)
	})
	defer desktop.Close()
	notifMgr := notification.NewManagerWithNotifier(desktop.Notify)
	connManager := connection.NewMemoryResidentConnectionManagerWithDisconnectHandler(func(user entity.User) {
		notifMgr.Publish(notification.Event{Type: notification.UserDisconnected, Data: map[string]string{"name": user.Name, "addr": user.Addr}})
	})
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
	securePath, securityErr := secretfs.Prepare(dbPath)
	if securityErr != nil {
		mainLogger.Fatal("Secret storage setup error", securityErr.Error(), nil)
		return
	}
	dbPath = securePath
	if len(os.Args) == 2 && os.Args[1] == "--secure-storage" {
		fmt.Println("Secret storage secured: " + dbPath)
		return
	}
	mainLogger.Info("Database path: "+dbPath, nil)
	db := sqlite.New(dbPath)
	if err := db.Connect(); err != nil {
		mainLogger.Fatal("Database connection error", err.Error(), nil)
	}
	defer db.Disconnect()
	if err := secretfs.PrivateTree(filepath.Dir(dbPath)); err != nil {
		mainLogger.Fatal("Secret permissions error", err.Error(), nil)
		return
	}

	pendingUsersStorage := pending.NewUserStorage()

	// TLS for the client↔server TCP channel. Self-signed cert is generated once
	// and stored next to the database; clients pin its fingerprint.
	tlsCert, err := tlsutil.LoadOrCreateCert(
		filepath.Join(filepath.Dir(dbPath), "tls", "server.crt"),
		filepath.Join(filepath.Dir(dbPath), "tls", "server.key"),
	)
	if err != nil {
		mainLogger.Fatal("TLS certificate setup error", err.Error(), nil)
	}
	if err := secretfs.PrivateTree(filepath.Dir(dbPath)); err != nil {
		mainLogger.Fatal("TLS permissions error", err.Error(), nil)
		return
	}
	tlsConfig, err := tlsutil.ServerConfig(tlsCert)
	if err != nil {
		mainLogger.Fatal("TLS configuration error", err.Error(), nil)
	}

	backendServer := rpc.NewServer(
		"Backend",
		rpc.NewBackendHandler(db, connManager, pendingUsersStorage, notifMgr),
		&rpc.ServerConfig{
			Network:   rpc.NetworkTCP,
			TLSConfig: tlsConfig,
		},
	)

	fingerprint, err := tlsutil.Fingerprint(tlsCert)
	if err != nil {
		mainLogger.Fatal("TLS fingerprint error", err.Error(), nil)
	}

	frontendServer := rpc.NewServer(
		"Frontend",
		rpc.NewFrontendHandler(db, connManager, pendingUsersStorage, notifMgr, fingerprint),
		&rpc.ServerConfig{
			Network:            rpc.NetworkPipe,
			InputBufferSize:    frontendPipeBufSize,
			OutputBufferSize:   frontendPipeBufSize,
			SecurityDescriptor: pipeSecurityDescriptor,
		},
	)

	backendPort := defaultBackendPort
	cliPortSet := false
	for i := 1; i < len(os.Args); i++ {
		if os.Args[i] == "--port" {
			if i+1 >= len(os.Args) {
				mainLogger.Error("Missing --port value", "", nil)
				return
			}
			backendPort, err = settingsapplication.ParseServerPort(os.Args[i+1])
			if err != nil {
				mainLogger.Error("Invalid --port value", err.Error(), nil)
				return
			}
			cliPortSet = true
			i++
		}
	}
	if !cliPortSet {
		if dbPort, err := db.GetSetting("server_port"); err == nil && dbPort != "" {
			backendPort, err = settingsapplication.ParseServerPort(dbPort)
			if err != nil {
				mainLogger.Error("Invalid saved server port", err.Error(), nil)
				return
			}
		}
	}
	backendAddr := fmt.Sprintf(":%d", backendPort)
	discoveryListener := discovery.New(db, backendPort)

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
	if err := configureFirewall(backendAddr); err != nil {
		mainLogger.Error("Firewall setup failed", err.Error(), nil)
		return
	}
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	stopCh := make(chan struct{}, 1)
	go func() {
		<-sigCh
		close(stopCh)
	}()
	startAndWait(backendAddr, frontendPipePath, discoveryListener, backendServer, frontendServer, connManager, stopCh)
}

func configureFirewall(backendAddr string) error {
	port, err := strconv.Atoi(backendAddr[1:])
	if err != nil {
		return err
	}
	exe, err := os.Executable()
	if err != nil {
		return err
	}
	return firewall.Sync(exe, port, discovery.Port)
}
