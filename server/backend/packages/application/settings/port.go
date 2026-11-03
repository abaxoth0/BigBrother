package settingsapplication

import (
	"fmt"
	"strconv"
)

func ParseServerPort(value string) (int, error) {
	for _, c := range value {
		if c < '0' || c > '9' {
			return 0, fmt.Errorf("invalid TCP port %q", value)
		}
	}
	port, err := strconv.Atoi(value)
	if err != nil || port < 1 || port > 65535 {
		return 0, fmt.Errorf("invalid TCP port %q", value)
	}
	return port, nil
}
