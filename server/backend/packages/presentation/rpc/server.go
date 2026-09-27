//go:build windows

package rpc

import (
	"fmt"
	"net"
	"strings"
	"sync"
	"time"

	"github.com/Microsoft/go-winio"
	"github.com/abaxoth0/Ain/errs"
)

const (
	NetworkPipe = "pipe"
	NetworkTCP  = "tcp"
)

type ServerConfig struct {
	Network            string // "pipe" or "tcp"
	InputBufferSize    int32
	OutputBufferSize   int32
	SecurityDescriptor string
}

type Handler interface {
	handle(conn net.Conn)
}

type Server struct {
	name     string
	config   *ServerConfig
	handler  Handler
	doneCh   chan struct{}
	stopCh   chan struct{}
	stopOnce sync.Once

	listener   net.Listener
	listenerMu sync.Mutex

	conns   map[net.Conn]struct{}
	connsMu sync.Mutex
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
		name:    name,
		config:  config,
		handler: handler,
		doneCh:  make(chan struct{}),
		stopCh:  make(chan struct{}),
		conns:   make(map[net.Conn]struct{}),
	}
}

func (s *Server) setListener(l net.Listener) {
	s.listenerMu.Lock()
	defer s.listenerMu.Unlock()
	s.listener = l
}

func (s *Server) getListener() net.Listener {
	s.listenerMu.Lock()
	defer s.listenerMu.Unlock()
	return s.listener
}

func (s *Server) trackConn(c net.Conn) {
	s.connsMu.Lock()
	defer s.connsMu.Unlock()
	s.conns[c] = struct{}{}
}

func (s *Server) untrackConn(c net.Conn) {
	s.connsMu.Lock()
	defer s.connsMu.Unlock()
	delete(s.conns, c)
}

// closeConns closes every active connection so blocked handler goroutines
// (e.g. a subscribed client) can exit during shutdown.
func (s *Server) closeConns() {
	s.connsMu.Lock()
	defer s.connsMu.Unlock()
	for c := range s.conns {
		c.Close()
	}
}

func (s *Server) Start(addr string) error {
	network := s.config.Network
	if network == "" {
		network = NetworkPipe
	}

	var listener net.Listener
	var err error

	switch network {
	case NetworkPipe:
		cfg := &winio.PipeConfig{
			MessageMode:        true,
			InputBufferSize:    s.config.InputBufferSize,
			OutputBufferSize:   s.config.OutputBufferSize,
			SecurityDescriptor: s.config.SecurityDescriptor,
		}
		listener, err = winio.ListenPipe(addr, cfg)

	case NetworkTCP:
		listener, err = net.Listen("tcp", addr)

	default:
		return fmt.Errorf("unsupported network: %s", network)
	}

	if err != nil {
		return err
	}
	s.setListener(listener)
	defer func() {
		s.setListener(nil)
		close(s.stopCh)
	}()

	log.Info(s.name+" server: listening on: "+addr+" ("+network+")", nil)
	var wg sync.WaitGroup

	for {
		select {
		case <-s.doneCh:
			listener.Close()
			wg.Wait()
			return nil
		default:
		}

		conn, err := listener.Accept()
		if err != nil {
			select {
			case <-s.doneCh:
				listener.Close()
				wg.Wait()
				return nil
			default:
				log.Error(s.name+" server: accept error", err.Error(), nil)
				continue
			}
		}

		wg.Add(1)
		s.trackConn(conn)
		go func(c net.Conn) {
			defer wg.Done()
			defer s.untrackConn(c)
			s.handler.handle(c)
		}(conn)
	}
}

const DefaultHandlerStopTimeout = time.Second * 10

func (s *Server) Stop(timeout time.Duration) error {
	s.stopOnce.Do(func() {
		close(s.doneCh)
		if l := s.getListener(); l != nil {
			l.Close() // unblock a pending Accept
		}
		s.closeConns() // unblock handler goroutines (subscribers, idle peers)
	})

	if timeout <= 0 {
		timeout = DefaultHandlerStopTimeout
	}
	select {
	case <-s.stopCh:
		return nil
	case <-time.After(timeout):
		return errs.StatusTimeout
	}
}
