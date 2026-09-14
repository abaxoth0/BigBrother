package pending

import (
	"bigbrother_server_backend/packages/domain/entity"
	"fmt"
	"sync"
	"time"
)

const PendingUserTimeout = time.Minute * 10

// TODO add interface

type UserStorage struct {
	users map[string]*entity.PendingUser
	mu    sync.Mutex
}

func NewUserStorage() *UserStorage {
	return &UserStorage{
		users: make(map[string]*entity.PendingUser),
	}
}

func (s *UserStorage) invalidate() {
	now := time.Now()
	for name, pu := range s.users {
		if now.Sub(pu.CreatedAt) > PendingUserTimeout {
			delete(s.users, name)
		}
	}
}

func (s *UserStorage) GetAll() []*entity.PendingUser {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.invalidate()

	users := make([]*entity.PendingUser, 0, len(s.users))
	for _, u := range s.users {
		users = append(users, u)
	}
	return users
}

func (s *UserStorage) Add(username, addr string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.invalidate()

	if _, ok := s.users[username]; ok {
		return fmt.Errorf("user already pending")
	}

	s.users[username] = &entity.PendingUser{
		Name:      username,
		Addr:      addr,
		CreatedAt: time.Now(),
	}

	return nil
}

func (s *UserStorage) Pop(username string) (*entity.User, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.invalidate()

	user, ok := s.users[username]
	if !ok {
		return nil, fmt.Errorf("no such pending user")
	}

	delete(s.users, username)
	return &entity.User{
		Name: user.Name,
		Addr: user.Addr,
	}, nil
}
