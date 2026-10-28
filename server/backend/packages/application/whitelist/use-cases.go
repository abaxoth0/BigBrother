package whitelistapplication

import "bigbrother_server_backend/packages/domain/entity"

type QueryHandler interface {
	GetWhitelists() ([]*entity.Whitelist, error)
	GetWhitelistByID(id string) (*entity.Whitelist, error)
	GetWhitelistByName(name string) (*entity.Whitelist, error)
	GetWhitelistID(whitelistName string) (string, error)
	GetWhitelistPresets() ([]entity.WhitelistPreset, error)

	GetWhitelistEntries(whitelistID string) ([]*entity.WhitelistEntry, error)
}

type CommandHandler interface {
	CreateWhitelist(name string, parentID string) error
	CreateTemporaryWhitelist(name string, durationSeconds int64, autoDelete bool) error
	UpdateWhitelistTimer(name string, durationSeconds int64, remainingSeconds *int64, autoDelete bool) error
	ResetWhitelistTimers(names []string) error
	SetWhitelistActive(name string, active bool) error
	SetWhitelistsActive(states map[string]bool) (bool, error)
	SaveWhitelistPreset(name string, activeWhitelists []string, replace bool) error
	LoadWhitelistPreset(name string) (bool, error)
	DeleteWhitelistPreset(name string) error
	ChangeWhitelistName(name string, newName string) error
	DeleteWhitelist(name string) error

	AddWhitelistEntry(value string, whitelistName string) error
	UpdateWhitelistEntry(value string, newValue string, whitelistName string) error
	DeleteWhitelistEntry(value string, whitelistName string) error
	ReplaceWhitelistEntries(entries []string, whitelistName string) error
}

type UseCases interface {
	QueryHandler
	CommandHandler
}
