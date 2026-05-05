//go:build windows

package rpc

import (
	"net"
	"strings"
	"sync"
	"time"

	"github.com/Microsoft/go-winio"
	"github.com/abaxoth0/Ain/errs"
)

type ServerConfig struct {
	InputBufferSize    int32
	OutputBufferSize   int32
	// Windows security descriptor in SDDL format.
	SecurityDescriptor string
}

type Handler interface {
	handle(conn net.Conn)
}

type Server struct {
	name 	string
	config	*ServerConfig
	handler Handler
	doneCh 	chan struct{}
	stopCh 	chan struct{}
}

func NewServer(name string, handler Handler, config *ServerConfig) *Server {
	if strings.ReplaceAll(name, " ", "") == "" {
		log.Panic("Failed to create server", "Missing or empty server name", nil)
	}
	if handler == nil {
		log.Panic("Failed to create \""+name+"\" server", "Missing server handler", nil)
	}
	if config == nil {
		log.Panic("Failed to create \""+name+"\" server", "Missing server config", nil)
	}
	return &Server{
		name: 	 name,
		config:  config,
		handler: handler,
		doneCh:  make(chan struct{}),
		stopCh:  make(chan struct{}),
	}
}

func (s *Server) Start(pipePath string) error {
	defer close(s.stopCh)

	cfg := &winio.PipeConfig{
		MessageMode: 		true,
		InputBufferSize: 	s.config.InputBufferSize,
		OutputBufferSize: 	s.config.OutputBufferSize,
		SecurityDescriptor: s.config.SecurityDescriptor,
	}

	listener, err := winio.ListenPipe(pipePath, cfg)
	if err != nil {
		return err
	}
	defer listener.Close()

	log.Info(s.name+" server: listening on: "+pipePath, nil)
	var wg sync.WaitGroup

	for {
		select {
		case <-s.doneCh:
			wg.Wait()
			return nil
		default:
			for {
				conn, err := listener.Accept()
				if err != nil {
					log.Error(s.name+" server: accept error", err.Error(), nil)
					continue
				}

				go func() {
					wg.Add(1)
					defer wg.Done()
					s.handler.handle(conn)
				}()
			}
		}
	}
}

const DefaultHandlerStopTimeout = time.Second * 10

func (s *Server) Stop(timeout time.Duration) error {
	if timeout == 0 {
		timeout = DefaultHandlerStopTimeout
	}
	close(s.doneCh)
	select {
	case <-s.stopCh:
		return nil
	case <-time.After(timeout):
		return errs.StatusTimeout
	}
}
