package whitelistapplication

import (
	"bigbrother_server_backend/packages/domain/entity"
	"fmt"
	"strconv"
	"strings"
)

func ReadPriority(value string) (priority int, body string, explicit bool, err error) {
	body = strings.TrimSpace(value)
	if !strings.HasPrefix(body, "[") {
		return
	}
	explicit = true
	end := strings.IndexByte(body, ']')
	if end <= 1 {
		err = fmt.Errorf("invalid priority: %q", value)
		return
	}
	digits := body[1:end]
	for _, c := range digits {
		if c < '0' || c > '9' {
			err = fmt.Errorf("invalid priority: %q", value)
			return
		}
	}
	n, parseErr := strconv.ParseInt(digits, 10, 32)
	if parseErr != nil {
		err = fmt.Errorf("priority must be between 0 and 2147483647")
		return
	}
	priority, body = int(n), strings.TrimSpace(body[end+1:])
	return
}

func FormatPriority(priority int, body string) string {
	if priority == 0 {
		return body
	}
	return fmt.Sprintf("[%d]%s", priority, body)
}

func NormalizePriorities(entries []*entity.WhitelistEntry) ([]*entity.WhitelistEntry, error) {
	type state struct {
		priority int
		seen     bool
	}
	states := map[string]*state{}
	result := make([]*entity.WhitelistEntry, 0, len(entries))
	for i, entry := range entries {
		value := strings.ToLower(strings.TrimSpace(entry.Value))
		if value == "" || strings.HasPrefix(value, "#") || strings.HasPrefix(value, ";") {
			continue
		}
		current, ok := states[entry.WhitelistID]
		if !ok {
			current = &state{}
			states[entry.WhitelistID] = current
		}
		priority, body, explicit, err := ReadPriority(value)
		if err != nil {
			return nil, fmt.Errorf("line %d: %w", i+1, err)
		}
		if explicit && body == "" {
			if current.seen {
				return nil, fmt.Errorf("line %d: default priority must precede all rules", i+1)
			}
			current.priority, current.seen = priority, true
			continue
		}
		current.seen = true
		if !explicit {
			priority = current.priority
		}
		if strings.ContainsAny(body, "[]") {
			return nil, fmt.Errorf("line %d: invalid priority prefix", i+1)
		}
		normalized := *entry
		normalized.Value = FormatPriority(priority, body)
		result = append(result, &normalized)
	}
	return result, nil
}
