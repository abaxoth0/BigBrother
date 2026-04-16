package rpc

import (
	"errors"
	"fmt"
	"slices"
	"time"
)

type ServerStatus struct {
	ConnectedClients []*Client
	Uptime			 time.Duration
}

type Client struct {
	Name 	 string
	Addr 	 string
	LastSeen time.Time
}

type whitelist map[string]bool

type RpcServer interface {
	GetWhitelist() []string
	SetWhitelist(domains []string) error

	AddDomain(domain string) error
	RemoveDomain(domain string) error

	GetClients() []Client
	KickClient(name string) error

	GetStatus() *ServerStatus
}

var startTime = (func() time.Time { return time.Now() })()

// TODO temp
var clients = []*Client{}

func newWhitelist(domains ...string) whitelist {
	newWl := make(whitelist, len(domains))
	for _, domain := range domains {
		newWl[domain] = true
	}
	return newWl
}

type Server struct {
	wl whitelist
}

func NewServer() *Server {
	return &Server{
		wl: newWhitelist(
			// TODO: temp
			"\"github.com\"",
			"*.wikipedia.org",
			"www.figma.com",
		),
	}
}

func (s *Server) GetWhitelist() []string {
	r := make([]string, 0, len(s.wl))
	for domain := range s.wl {
		r = append(r, domain)
	}
	return r
}

func (s *Server) SetWhitelist(domains []string) error {
	s.wl = newWhitelist(domains...)
	return nil
}

func (s *Server) AddDomain(domain string) error {
	if s.wl[domain] {
		return errors.New("domain already in whitelist")
	}
	s.wl[domain] = true
	return nil
}

func (s *Server) RemoveDomain(domain string) error {
	if !s.wl[domain] {
		return errors.New("domain is not in whitelist")
	}
	delete(s.wl, domain)
	return nil
}

func (s *Server) GetClients() []*Client {
	return clients
}

func (s *Server) AddClient(client *Client) error {
	ok := slices.ContainsFunc(clients, func(c *Client) bool {
		return client.Addr == c.Addr
	})
	if !ok {
		return fmt.Errorf("client with address %s aleady exists", client.Addr)
	}
	clients = append(clients, client)
	return nil
}

func (s *Server) KickClient(name string) error {
	for i, client := range clients {
		if client.Name == name {
			clients = append(clients[:i], clients[i+1:]...)
			return nil
		}
	}
	return errors.New("client not found")
}

func (s *Server) GetStatus() *ServerStatus {
	return &ServerStatus{
		ConnectedClients: clients,
		Uptime: time.Since(startTime),
	}
}
