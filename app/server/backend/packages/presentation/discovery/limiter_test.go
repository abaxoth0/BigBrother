package discovery

import (
	"fmt"
	"net"
	"testing"
	"time"
)

func TestSourcePortAndMappedIPShareBudget(t *testing.T) {
	var l responseLimiter
	now := time.Now()
	addr := &net.UDPAddr{IP: net.ParseIP("192.0.2.1"), Port: 1000}
	if !l.allow(addr, now) {
		t.Fatal("initial request denied")
	}
	addr.Port++
	addr.IP = net.ParseIP("::ffff:192.0.2.1")
	if l.allow(addr, now.Add(minResponseInterval-time.Nanosecond)) {
		t.Fatal("changed source port/IP representation bypassed budget")
	}
	if !l.allow(addr, now.Add(minResponseInterval)) {
		t.Fatal("budget did not recover")
	}
	addr.IP = net.ParseIP("192.0.2.2")
	if !l.allow(addr, now) {
		t.Fatal("independent source denied")
	}
}

func TestSourceMapIsBoundedAndRecovers(t *testing.T) {
	var l responseLimiter
	now := time.Now()
	for i := 0; i < maxResponseSources; i++ {
		if !l.allow(&net.UDPAddr{IP: net.ParseIP(fmt.Sprintf("2001:db8::%x", i+1))}, now) {
			t.Fatal("capacity reduced")
		}
	}
	extra := &net.UDPAddr{IP: net.ParseIP("192.0.2.10")}
	if l.allow(extra, now) || len(l.last) != maxResponseSources {
		t.Fatal("map exceeded limit")
	}
	if !l.allow(extra, now.Add(minResponseInterval)) {
		t.Fatal("expired sources not reclaimed")
	}
}
