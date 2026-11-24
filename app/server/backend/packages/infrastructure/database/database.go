package database

import (
	SettingsApplication "bigbrother_server_backend/packages/application/settings"
	UserApplication "bigbrother_server_backend/packages/application/user"
	WhitelistApplication "bigbrother_server_backend/packages/application/whitelist"
)

type Connection interface {
	Connect() error
	Disconnect() error
}

type DBInstance interface {
	Connection
	UserApplication.UseCases
	WhitelistApplication.UseCases
	SettingsApplication.UseCases
}
