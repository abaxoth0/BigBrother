package userapplication

import "bigbrother_server_backend/packages/domain/entity"

type QueryHandler interface {
	GetUserByName(username string) (*entity.User, error)
	GetUserByAddr(userAddr string) (*entity.User, error)
}

type CommandHandler interface {
	CreateUser(name, addr string) (string, error)
	ChangeUserAddr(username string, newAddr string) error
	ChangeUserName(username string, newUsername string) error
	ChangeUsersWhitelist(newWhitelistName string, usernames ...string) error
	DeleteUsers(usernames ...string) error
}

type UseCases interface {
	QueryHandler
	CommandHandler
}
