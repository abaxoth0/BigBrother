package processlist

import (
	"fmt"
	"strings"
	"unicode"
	"unicode/utf8"

	"bigbrother_server_backend/packages/application/rules"
)

const Setting = "process_blacklist"
const EnabledSetting = "process_blacklist_enabled"
const maxBytes = 8192

func EnabledValue(value string) (string, error) {
	switch value {
	case "":
		return "1", nil
	case "0", "1":
		return value, nil
	default:
		return "", fmt.Errorf("invalid process blacklist setting %q", value)
	}
}

// Compile validates executable names and expands default priorities for client monitors.
func Compile(text string) ([]string, error) {
	if len(text) > maxBytes || !utf8.ValidString(text) {
		return nil, fmt.Errorf("expected at most %d bytes of UTF-8 text", maxBytes)
	}

	var source rules.Source
	var result []string
	size := 0
	for i, line := range strings.Split(text, "\n") {
		priority, body, err := source.Next(line)
		if err != nil {
			return nil, fmt.Errorf("line %d: %w", i+1, err)
		}
		if body == "" {
			continue
		}

		name := strings.TrimPrefix(body, "!")
		stem, exe := strings.CutSuffix(name, ".exe")
		prefix := strings.TrimSuffix(stem, "*")
		if !exe || prefix == "" || len(name) > 255 ||
			strings.ContainsAny(prefix, `<>:"/\|?*!`) || strings.IndexFunc(prefix, unicode.IsControl) >= 0 {
			return nil, fmt.Errorf("line %d: expected an executable name such as game.exe or game*.exe", i+1)
		}

		value := rules.FormatPriority(priority, body)
		size += len(value) + 1
		if size > maxBytes {
			return nil, fmt.Errorf("expanded process rules exceed %d bytes", maxBytes)
		}
		result = append(result, value)
	}
	return result, nil
}
