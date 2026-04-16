package database

import (
	UserApplication "bigbrother_server_backend/packages/application/user"
	WhitelistApplication "bigbrother_server_backend/packages/application/whitelist"
	"bigbrother_server_backend/packages/database/sqlite"
)

type Connection interface {
	Connect() error
	Disconnect() error
}

type DBInstance interface {
	Connection
	UserApplication.UseCases
	WhitelistApplication.UseCases
}
