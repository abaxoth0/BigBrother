package rpc

import (
	"net"
	"sync"
	"time"
)

// ipLimiter is a per-address token bucket used to throttle connection spikes.
type ipLimiter struct {
	mu     sync.Mutex
	m      map[string]*ipBucket
	rate   float64
	burst  int
	prunes int
}

type ipBucket struct {
	tokens float64
	last   time.Time
}

func newIPLimiter(rate float64, burst int) *ipLimiter {
	return &ipLimiter{m: make(map[string]*ipBucket), rate: rate, burst: burst}
}

// allow reports whether a connection from ip is within the rate budget.
func (l *ipLimiter) allow(ip string) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	b, ok := l.m[ip]
	if !ok {
		b = &ipBucket{tokens: float64(l.burst), last: time.Now()}
		l.m[ip] = b
	}
	now := time.Now()
	dt := now.Sub(b.last).Seconds()
	if dt > 0 {
		b.tokens += dt * l.rate
		if b.tokens > float64(l.burst) {
			b.tokens = float64(l.burst)
		}
		b.last = now
	}
	l.pruneLocked()
	if b.tokens >= 1 {
		b.tokens--
		return true
	}
	return false
}

func (l *ipLimiter) pruneLocked() {
	if len(l.m) < 4096 {
		l.prunes = 0
		return
	}
	l.prunes++
	if l.prunes%16 != 0 {
		return
	}
	now := time.Now()
	for k, b := range l.m {
		if now.Sub(b.last) > time.Minute {
			delete(l.m, k)
		}
	}
}

func hostOf(a net.Addr) string {
	if a == nil {
		return ""
	}
	host, _, err := net.SplitHostPort(a.String())
	if err != nil {
		return a.String()
	}
	return host
}
