package discovery

import (
	"fmt"
	"net"
	"strings"
	"sync"
	"time"

	settingsapplication "bigbrother_server_backend/packages/application/settings"
	"bigbrother_server_backend/packages/common/log"
	"bigbrother_server_backend/packages/infrastructure/database"

	"github.com/abaxoth0/Ain/logger"
)

type discLogger = *logger.Source[*logger.FileLogger]

const (
	Port           = 42069
	DiscoveryMagic = "BIGBROTHER_DISCOVERY"
	ResponseMagic  = "BIGBROTHER_DISCOVERY_RESPONSE"
	ReadTimeout    = time.Second * 2
)

type Listener struct {
	db       database.DBInstance
	conn     *net.UDPConn
	stopCh   chan struct{}
	doneCh   chan struct{}
	stopOnce sync.Once
	logger   discLogger

	limiter    responseLimiter
	serverPort int
}

func New(db database.DBInstance, serverPort int) *Listener {
	return &Listener{
		db:         db,
		stopCh:     make(chan struct{}),
		doneCh:     make(chan struct{}),
		logger:     logger.NewSource("DISCOVERY", log.DefaultLogger),
		serverPort: serverPort,
	}
}

func (l *Listener) Start() error {
	addr := &net.UDPAddr{Port: Port}
	conn, err := net.ListenUDP("udp", addr)
	if err != nil {
		return fmt.Errorf("discovery listen: %w", err)
	}
	l.conn = conn
	l.logger.Info(fmt.Sprintf("Listening on UDP :%d", Port), nil)

	go l.serve()
	return nil
}

func (l *Listener) Stop() {
	l.stopOnce.Do(func() {
		close(l.stopCh)
		if l.conn != nil {
			l.conn.Close()
		}
	})
	if l.conn != nil {
		<-l.doneCh
	}
}

func (l *Listener) getLocalIP() string {
	interfaces, err := net.Interfaces()
	if err != nil {
		return "0.0.0.0"
	}
	for _, iface := range interfaces {
		if iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		if iface.Flags&net.FlagUp == 0 {
			continue
		}
		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, addr := range addrs {
			ipnet, ok := addr.(*net.IPNet)
			if !ok {
				continue
			}
			ipv4 := ipnet.IP.To4()
			if ipv4 != nil {
				return ipv4.String()
			}
		}
	}
	return "0.0.0.0"
}

func (l *Listener) serve() {
	defer close(l.doneCh)

	buf := make([]byte, 1024)
	for {
		select {
		case <-l.stopCh:
			return
		default:
			l.conn.SetReadDeadline(time.Now().Add(ReadTimeout))
			n, rAddr, err := l.conn.ReadFromUDP(buf)
			if err != nil {
				if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
					continue
				}
				select {
				case <-l.stopCh:
					return
				default:
					l.logger.Error("Read error", err.Error(), nil)
					continue
				}
			}

			msg := strings.TrimSpace(string(buf[:n]))
			if msg == DiscoveryMagic || strings.HasPrefix(msg, DiscoveryMagic+"\n") {
				if !l.limiter.allow(rAddr, time.Now()) {
					continue // rate-limited reply to this source
				}
				serverName, _ := l.db.GetSetting("server_name")
				if serverName == "" {
					serverName = settingsapplication.DefaultServerName
				}
				localIP := l.getLocalIP()
				response := fmt.Sprintf("%s\n%s\n%s\n%d\n", ResponseMagic, serverName, localIP, l.serverPort)
				l.conn.WriteToUDP([]byte(response), rAddr)
				if log.DiagnosticsEnabled {
					l.logger.Debug(fmt.Sprintf("Discovery response sent to %s: name=%s ip=%s port=%d", rAddr.String(), serverName, localIP, l.serverPort), nil)
				}
			}
		}
	}
}
