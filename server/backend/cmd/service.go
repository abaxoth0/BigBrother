//go:build windows

package main

import (
	"bigbrother_server_backend/packages/presentation/discovery"
	"bigbrother_server_backend/packages/presentation/rpc"
	"fmt"

	"golang.org/x/sys/windows/svc"
)

func isService() bool {
	isSvc, err := svc.IsWindowsService()
	if err != nil {
		mainLogger.Info("Failed to check if running as service: "+err.Error(), nil)
	}
	return isSvc
}

func runService(backendAddr, frontendPipePath string, discoveryListener *discovery.Listener, backendServer, frontendServer *rpc.Server) {
	handler := &serviceHandler{
		backendAddr:       backendAddr,
		frontendPipePath:  frontendPipePath,
		discoveryListener: discoveryListener,
		backendServer:     backendServer,
		frontendServer:    frontendServer,
	}
	if err := svc.Run(serviceName, handler); err != nil {
		mainLogger.Fatal("Service run failed", err.Error(), nil)
	}
}

type serviceHandler struct {
	backendAddr       string
	frontendPipePath  string
	discoveryListener *discovery.Listener
	backendServer     *rpc.Server
	frontendServer    *rpc.Server
}

func (h *serviceHandler) Execute(args []string, r <-chan svc.ChangeRequest, changes chan<- svc.Status) (svcSpecificEC bool, exitCode uint32) {
	changes <- svc.Status{State: svc.StartPending}

	stopCh := make(chan struct{})
	go startAndWait(h.backendAddr, h.frontendPipePath, h.discoveryListener, h.backendServer, h.frontendServer, stopCh)

	changes <- svc.Status{State: svc.Running, Accepts: svc.AcceptStop | svc.AcceptShutdown}

	for {
		select {
		case c := <-r:
			switch c.Cmd {
			case svc.Stop, svc.Shutdown:
				mainLogger.Info("Service stop requested", nil)
				changes <- svc.Status{State: svc.StopPending}
				close(stopCh)
				return false, 0
			default:
				mainLogger.Info(fmt.Sprintf("Unexpected service control request: %d", c.Cmd), nil)
			}
		}
	}
}
