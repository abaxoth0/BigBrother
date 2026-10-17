package updates

import (
	"encoding/json"
	"errors"
	"regexp"
	"strings"
)

const MaxSize = 1024 * 1024 * 1024
const MaxMetadataSize = 128 * 1024

var versionPattern = regexp.MustCompile(`^[0-9]{1,5}\.[0-9]{1,5}\.[0-9]{1,5}(\.[0-9]{1,5})?$`)
var hashPattern = regexp.MustCompile(`^[a-f0-9]{64}$`)

type Manifest struct {
	Version      string        `json:"version"`
	File         string        `json:"file"`
	Location     string        `json:"location"`
	Size         int64         `json:"size"`
	SHA256       string        `json:"sha256"`
	ReleaseNotes *ReleaseNotes `json:"release_notes,omitempty"`
}

type ReleaseNotes struct {
	Version  string           `json:"version"`
	Sections []ReleaseSection `json:"sections"`
}

type ReleaseSection struct {
	Kind  string   `json:"kind"`
	Items []string `json:"items"`
}

func Parse(data string) (*Manifest, error) {
	var m Manifest
	if len(data) >= MaxMetadataSize || json.Unmarshal([]byte(data), &m) != nil {
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
	base := m
	base.ReleaseNotes = nil
	baseData, _ := json.Marshal(base)
	if len(baseData) >= 8192 {
		return errors.New("client update reference is too large")
	}
	if m.ReleaseNotes != nil {
		if strings.TrimSpace(m.ReleaseNotes.Version) == "" || m.ReleaseNotes.Sections == nil || len(m.ReleaseNotes.Sections) > 6 {
			return errors.New("invalid client release notes")
		}
		for _, section := range m.ReleaseNotes.Sections {
			if section.Items == nil {
				return errors.New("invalid client release notes items")
			}
			switch section.Kind {
			case "added", "removed", "changed", "fixed", "deprecated", "security":
			default:
				return errors.New("invalid client release notes section")
			}
			for _, item := range section.Items {
				if strings.TrimSpace(item) == "" {
					return errors.New("empty client release note")
				}
			}
		}
	}
	encoded, err := json.Marshal(m)
	if err != nil || len(encoded) >= MaxMetadataSize {
		return errors.New("client update metadata is too large")
	}
	return nil
}
