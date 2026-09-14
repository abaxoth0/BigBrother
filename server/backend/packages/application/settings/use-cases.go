package settingsapplication

// DefaultServerName is the server name reported when none is configured.
const DefaultServerName = "BigBrother Server"

type QueryHandler interface {
	GetSetting(key string) (string, error)
}

type CommandHandler interface {
	SetSetting(key, value string) error
}

type UseCases interface {
	QueryHandler
	CommandHandler
}
