package discovery

import (
	"net"
	"strings"
	"testing"
	"time"

	"bigbrother_server_backend/packages/infrastructure/database"
)

type discoveryDB struct{ database.DBInstance }

func (discoveryDB) GetSetting(key string) (string, error) {
	if key == "server_name" {
		return "Test Server", nil
	}
	return "1984", nil
}

func TestUDPAdvertisesRunningPortAndThrottlesAcrossSockets(t *testing.T) {
	l := New(discoveryDB{}, 4443)
	var err error
	l.conn, err = net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	go l.serve()
	defer l.Stop()
	first, err := net.DialUDP("udp4", nil, l.conn.LocalAddr().(*net.UDPAddr))
	if err != nil {
		t.Fatal(err)
	}
	defer first.Close()
	second, err := net.DialUDP("udp4", nil, l.conn.LocalAddr().(*net.UDPAddr))
	if err != nil {
		t.Fatal(err)
	}
	defer second.Close()
	if _, err := first.Write([]byte(DiscoveryMagic)); err != nil {
		t.Fatal(err)
	}
	first.SetReadDeadline(time.Now().Add(time.Second))
	buf := make([]byte, 1024)
	n, err := first.Read(buf)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(string(buf[:n]), ResponseMagic+"\nTest Server\n") || !strings.HasSuffix(string(buf[:n]), "\n4443\n") {
		t.Fatalf("wrong advertisement: %q", buf[:n])
	}
	if _, err := second.Write([]byte(DiscoveryMagic)); err != nil {
		t.Fatal(err)
	}
	second.SetReadDeadline(time.Now().Add(100 * time.Millisecond))
	if _, err := second.Read(buf); err == nil {
		t.Fatal("second UDP source port bypassed limiter")
	} else if e, ok := err.(net.Error); !ok || !e.Timeout() {
		t.Fatal(err)
	}
}
