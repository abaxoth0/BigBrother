package entity

import "time"

type User struct {
	Id          string
	Name        string
	Addr        string
	WhitelistID string
}

type PendingUser struct {
	Name      string
	Addr      string
	CreatedAt time.Time
}
