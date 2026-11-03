package firewall

import (
	"errors"
	"strings"
	"testing"
)

func TestRulesUseActualPortAndRestrictDiscovery(t *testing.T) {
	err := syncRules(`C:\Program Files\O'Brien\server.exe`, 4443, 42069, func(script string) error {
		for _, want := range []string{"-LocalPort 4443", "-LocalPort 42069 -RemoteAddress LocalSubnet", "-Profile Private,Domain", `-Program 'C:\Program Files\O''Brien\server.exe'`, LegacyTCPRule, "Remove-NetFirewallRule", "$ErrorActionPreference = 'Stop'", "exit 1"} {
			if !strings.Contains(script, want) {
				t.Errorf("missing %q", want)
			}
		}
		if strings.Contains(script, "-LocalPort 1984") {
			t.Fatal("hardcoded TCP port")
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
}

func TestFirewallFailurePropagates(t *testing.T) {
	failure := errors.New("access denied")
	if err := syncRules("server.exe", 1984, 42069, func(string) error { return failure }); !errors.Is(err, failure) {
		t.Fatalf("failure hidden: %v", err)
	}
	for _, port := range []int{0, -1, 65536} {
		if err := syncRules("server.exe", port, 42069, func(string) error { t.Fatal("invalid port executed"); return nil }); err == nil {
			t.Fatal("invalid port accepted")
		}
	}
}
