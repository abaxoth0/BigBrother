package entity

type WhitelistEntry struct {
	ID      string
	Value   string
	Sources []string `json:"sources,omitempty"`
}

type Whitelist struct {
	ID        string
	Name      string
	ParentID  string
	IsActive  bool
	Temporary *WhitelistTimer `json:"temporary,omitempty"`
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

type WhitelistTimer struct {
	DurationSeconds  int64   `json:"durationSeconds"`
	RemainingSeconds float64 `json:"remainingSeconds"`
	AutoDelete       bool    `json:"autoDelete"`
}
