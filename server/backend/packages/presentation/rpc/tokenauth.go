package rpc

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/sha256"
	"encoding/hex"
	"math/big"
)

// signPublicPointSize is the size of an uncompressed SEC1 EC point on P-256.
const signPublicPointSize = 65

// verifyClientSignature verifies an ECDSA-P256/SHA-256 signature over the raw
// bytes of username||nonce, using the uncompressed public point hex stored for
// the user. The message bytes must match exactly what the client signed.
func verifyClientSignature(signPubHex, username, nonce, sigHex string) bool {
	pub, ok := parseP256Public(signPubHex)
	if !ok {
		return false
	}
	sig, err := hex.DecodeString(sigHex)
	if err != nil {
		return false
	}
	digest := sha256.Sum256([]byte(username + nonce))
	return ecdsa.VerifyASN1(pub, digest[:], sig)
}

func parseP256Public(hexPoint string) (*ecdsa.PublicKey, bool) {
	b, err := hex.DecodeString(hexPoint)
	if err != nil || len(b) != signPublicPointSize || b[0] != 0x04 {
		return nil, false
	}
	return &ecdsa.PublicKey{
		Curve: elliptic.P256(),
		X:     new(big.Int).SetBytes(b[1:33]),
		Y:     new(big.Int).SetBytes(b[33:65]),
	}, true
}
