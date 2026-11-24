package rules

import (
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

// Source resolves the default priority of one ordered source list.
type Source struct {
	priority int
	seen     bool
}

// Next returns an empty body for comments, blank lines and the default declaration.
func (s *Source) Next(value string) (int, string, error) {
	value = strings.ToLower(strings.TrimSpace(value))
	if value == "" || strings.HasPrefix(value, "#") || strings.HasPrefix(value, ";") {
		return 0, "", nil
	}
	priority, body, explicit, err := ReadPriority(value)
	if err != nil {
		return 0, "", err
	}
	if explicit && body == "" {
		if s.seen {
			return 0, "", fmt.Errorf("default priority must precede all rules")
		}
		s.priority, s.seen = priority, true
		return 0, "", nil
	}
	s.seen = true
	if !explicit {
		priority = s.priority
	}
	if strings.ContainsAny(body, "[]") {
		return 0, "", fmt.Errorf("invalid priority prefix")
	}
	return priority, body, nil
}
