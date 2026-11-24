package rpc

import (
	"crypto/rand"
	"encoding/hex"
	"sync"
	"time"
)

const challengeTTL = 60 * time.Second

type challengeEntry struct {
	nonce   string
	expires time.Time
}

var (
	challengeMu sync.Mutex
	challenges  = map[string]challengeEntry{}
)

// newChallenge issues a single-use nonce for a username, used to prove the
// client holds its registered Ed25519 key when fetching its token.
func newChallenge(username string) (string, error) {
	b := make([]byte, 16)
	if _, err := rand.Read(b); err != nil {
		return "", err
	}
	nonce := hex.EncodeToString(b)
	challengeMu.Lock()
	challenges[username] = challengeEntry{nonce: nonce, expires: time.Now().Add(challengeTTL)}
	challengeMu.Unlock()
	return nonce, nil
}

// consumeChallenge verifies and consumes (single-use) a challenge nonce.
func consumeChallenge(username, nonce string) bool {
	challengeMu.Lock()
	defer challengeMu.Unlock()
	c, ok := challenges[username]
	if !ok || time.Now().After(c.expires) || c.nonce != nonce {
		return false
	}
	delete(challenges, username)
	return true
}
