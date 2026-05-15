package settingsapplication

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
