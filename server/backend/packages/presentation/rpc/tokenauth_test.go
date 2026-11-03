package rpc

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"math/big"
	"testing"
)

// makeTestPoint returns an uncompressed P-256 SEC1 point hex for a fresh key.
func makeTestPoint(t *testing.T) (curvePtHex string, sign func(msg []byte) string) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatalf("genkey: %v", err)
	}
	xb := key.PublicKey.X.FillBytes(make([]byte, 32))
	yb := key.PublicKey.Y.FillBytes(make([]byte, 32))
	pt := append([]byte{0x04}, xb...)
	pt = append(pt, yb...)
	return hex.EncodeToString(pt), func(msg []byte) string {
		digest := sha256.Sum256(msg)
		r, s, err := ecdsa.Sign(rand.Reader, key, digest[:])
		if err != nil {
			t.Fatalf("sign: %v", err)
		}
		sig, err := encodeASN1Sig(r, s)
		if err != nil {
			t.Fatalf("encode: %v", err)
		}
		return hex.EncodeToString(sig)
	}
}

func TestVerifyClientSignature(t *testing.T) {
	ptHex, sign := makeTestPoint(t)
	username := "dev"
	nonce := "abc123"

	sig := sign([]byte(username + nonce))
	if !verifyClientSignature(ptHex, username, nonce, sig) {
		t.Fatal("expected valid signature to verify")
	}

	// Wrong message must fail.
	if verifyClientSignature(ptHex, username, nonce+"x", sig) {
		t.Fatal("expected mismatch to fail")
	}
	// Garbage signature must fail.
	if verifyClientSignature(ptHex, username, nonce, "deadbeef") {
		t.Fatal("expected garbage signature to fail")
	}
	// Bad public key must fail.
	if verifyClientSignature("00", username, nonce, sig) {
		t.Fatal("expected bad key to fail")
	}
}

// encodeASN1Sig encodes an ECDSA (r,s) as a DER SEQUENCE of two integers.
func encodeASN1Sig(r, s *big.Int) ([]byte, error) {
	intBytes := func(v *big.Int) []byte {
		sig := v.Bytes()
		if sig[0]&0x80 != 0 {
			sig = append([]byte{0}, sig...)
		}
		return append([]byte{0x02, byte(len(sig))}, sig...)
	}
	body := append(intBytes(r), intBytes(s)...)
	if len(body) > 0x7F {
		return nil, fmt.Errorf("signature too long: %d", len(body))
	}
	return append([]byte{0x30, byte(len(body))}, body...), nil
}
