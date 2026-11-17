package settingsapplication

import (
	"errors"
	"testing"
)

type firewallSettings struct {
	value string
	err   error
}

func (s firewallSettings) GetSetting(key string) (string, error) {
	if key != FirewallPublicEnabled {
		return "", errors.New("unexpected setting")
	}
	return s.value, s.err
}

func TestPublicFirewallPreference(t *testing.T) {
	for _, tc := range []struct {
		value string
		want  bool
	}{{"", true}, {"1", true}, {"0", false}} {
		got, err := PublicFirewallEnabled(firewallSettings{value: tc.value})
		if err != nil || got != tc.want {
			t.Fatalf("value %q: got %t, %v", tc.value, got, err)
		}
	}
	failure := errors.New("read failed")
	if _, err := PublicFirewallEnabled(firewallSettings{err: failure}); !errors.Is(err, failure) {
		t.Fatalf("read failure hidden: %v", err)
	}
	if _, err := PublicFirewallEnabled(firewallSettings{value: "invalid"}); err == nil {
		t.Fatal("invalid preference accepted")
	}
}
