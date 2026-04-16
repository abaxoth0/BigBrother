package entity

type UserState uint8

const (
	Disconnected UserState = iota
	Connected
)

type User struct {
	Id 			string
	Name 		string
	Addr 		string
	State 		UserState
	WhitelistID string
}
