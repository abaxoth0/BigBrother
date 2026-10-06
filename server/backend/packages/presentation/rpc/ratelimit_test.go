package rpc

import (
	"net"
	"testing"
	"time"
)

type testAddr string

func (t testAddr) Network() string { return "test" }
func (t testAddr) String() string  { return string(t) }

func addr(s string) net.Addr { return testAddr(s) }

func TestIPLimiterAllowsBurstThenThrottles(t *testing.T) {
	l := newIPLimiter(10, 5) // 10/s, burst 5

	// First burst of 5 is allowed.
	for i := 0; i < 5; i++ {
		if !l.allow("1.2.3.4") {
			t.Fatalf("expected burst token %d to be allowed", i+1)
		}
	}
	// Immediately following are throttled.
	if l.allow("1.2.3.4") {
		t.Fatal("expected token after burst to be denied")
	}
}

func TestIPLimiterRefills(t *testing.T) {
	l := newIPLimiter(1, 1) // 1 token/s, burst 1
	if !l.allow("1.2.3.4") {
		t.Fatal("first should be allowed")
	}
	if l.allow("1.2.3.4") {
		t.Fatal("second immediately should be denied")
	}
	time.Sleep(110 * time.Millisecond) // still < 1s
	if l.allow("1.2.3.4") {
		t.Fatal("expected still throttled before a full second")
	}
	time.Sleep(1 * time.Second)
	if !l.allow("1.2.3.4") {
		t.Fatal("expected refilled token to be allowed")
	}
}

func TestIPLimiterIsolation(t *testing.T) {
	l := newIPLimiter(1, 3)
	l.allow("a")
	l.allow("a")
	l.allow("a")
	if l.allow("a") {
		t.Fatal("a should be exhausted")
	}
	// Different address is independent.
	if !l.allow("b") {
		t.Fatal("b should not be affected by a")
	}
}

func TestHostOf(t *testing.T) {
	if got := hostOf(addr("1.2.3.4:1984")); got != "1.2.3.4" {
		t.Fatalf("unexpected host: %q", got)
	}
	if got := hostOf(nil); got != "" {
		t.Fatalf("expected empty for nil, got %q", got)
	}
}
