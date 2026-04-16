package whitelistapplication

import "bigbrother_server_backend/packages/domain/entity"

type QueryHandler interface {
	GetWhitelistByID(id string) (*entity.Whitelist, error)
	GetWhitelistByName(name string) (*entity.Whitelist, error)
	GetWhitelistID(whitelistName string) (string, error)

	GetWhitelistEntries(whitelistID string) ([]*entity.WhitelistEntry, error)
}

type CommandHandler interface {
	CreateWhitelist(name string, parentID string) error
	ChangeWhitelistName(name string, newName string) error
	DeleteWhitelist(name string) error

	AddWhitelistEntry(value string, whitelistName string) error
	UpdateWhitelistEntry(value string, newValue string, whitelistName string) error
	DeleteWhitelistEntry(value string, whitelistName string) error
}

type UseCases interface {
	QueryHandler
	CommandHandler
}
