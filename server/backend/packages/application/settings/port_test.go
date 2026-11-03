package settingsapplication

import "testing"

func TestParseServerPort(t *testing.T) {
	for _, value := range []string{"", "0", "65536", "-1", "+80", "1984junk", "80\n", " 80", "9999999999999999999999"} {
		if _, err := ParseServerPort(value); err == nil {
			t.Errorf("accepted %q", value)
		}
	}
	for value, want := range map[string]int{"1": 1, "65535": 65535, "1984": 1984, "04443": 4443} {
		if got, err := ParseServerPort(value); err != nil || got != want {
			t.Errorf("%q: %d %v", value, got, err)
		}
	}
}
