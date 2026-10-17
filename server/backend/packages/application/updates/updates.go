package updates

import (
	"encoding/json"
	"errors"
	"regexp"
	"strings"
)

const MaxSize = 1024 * 1024 * 1024

var versionPattern = regexp.MustCompile(`^[0-9]{1,5}\.[0-9]{1,5}\.[0-9]{1,5}(\.[0-9]{1,5})?$`)
var hashPattern = regexp.MustCompile(`^[a-f0-9]{64}$`)

type Manifest struct {
	Version  string `json:"version"`
	File     string `json:"file"`
	Location string `json:"location"`
	Size     int64  `json:"size"`
	SHA256   string `json:"sha256"`
}

func Parse(data string) (*Manifest, error) {
	var m Manifest
	if len(data) > 8192 || json.Unmarshal([]byte(data), &m) != nil {
		return nil, errors.New("invalid client update metadata")
	}
	if err := m.Validate(); err != nil {
		return nil, err
	}
	return &m, nil
}

func (m Manifest) Validate() error {
	absolute := (len(m.Location) > 3 && m.Location[1] == ':' && m.Location[2] == '\\') || strings.HasPrefix(m.Location, `\\`)
	if !versionPattern.MatchString(m.Version) || m.Size < 2 || m.Size > MaxSize ||
		!hashPattern.MatchString(m.SHA256) || !absolute || len(m.Location) > 2048 ||
		strings.ContainsAny(m.Location, "\r\n\x00\"") || !strings.HasSuffix(strings.ToLower(m.Location), ".exe") {
		return errors.New("invalid client update metadata")
	}
	encoded, err := json.Marshal(m)
	if err != nil || len(encoded) >= 8192 {
		return errors.New("client update metadata is too large")
	}
	return nil
}
