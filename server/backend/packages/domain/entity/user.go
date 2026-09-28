package entity

import "time"

type User struct {
	Id          string
	Name        string
	Addr        string
	WhitelistID string
	Token       string
	SignPublic  string // Ed25519 public key (hex, 64 chars) set at approval
}

type PendingUser struct {
	Name       string
	Addr       string
	CreatedAt  time.Time
	SignPublic string // Ed25519 public key (hex) supplied at registration
}
