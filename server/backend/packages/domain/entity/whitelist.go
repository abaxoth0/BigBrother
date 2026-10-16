package entity

type WhitelistEntry struct {
	ID    string
	Value string
}

type Whitelist struct {
	ID       string
	Name     string
	ParentID string
	IsActive bool
}

type WhitelistConflict struct {
	Pattern    string   `json:"pattern"`
	AllowedBy  []string `json:"allowedBy"`
	ExcludedBy []string `json:"excludedBy"`
}

type WhitelistComposition struct {
	Entries   []*WhitelistEntry   `json:"entries"`
	Conflicts []WhitelistConflict `json:"conflicts"`
}
