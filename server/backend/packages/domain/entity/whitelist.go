package entity

type WhitelistEntry struct {
	ID    string
	Value string
}

type Whitelist struct {
	ID       string
	Name     string
	ParentID string
}
