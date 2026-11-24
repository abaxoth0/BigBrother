package discovery

import (
	"net"
	"sync"
	"time"
)

const minResponseInterval = time.Second / 2
const maxResponseSources = 4096

type responseLimiter struct {
	mu   sync.Mutex
	last map[string]time.Time
}

func (l *responseLimiter) allow(src *net.UDPAddr, now time.Time) bool {
	if src == nil || src.IP == nil {
		return false
	}
	key := src.IP.String()
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.last == nil {
		l.last = make(map[string]time.Time)
	}
	if last, ok := l.last[key]; ok {
		if now.Sub(last) < minResponseInterval {
			return false
		}
	} else if len(l.last) >= maxResponseSources {
		for k, last := range l.last {
			if now.Sub(last) >= minResponseInterval {
				delete(l.last, k)
			}
		}
		if len(l.last) >= maxResponseSources {
			return false
		}
	}
	l.last[key] = now
	return true
}
