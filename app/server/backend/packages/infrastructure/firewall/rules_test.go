package firewall

import (
	settingsapplication "bigbrother_server_backend/packages/application/settings"
	"bigbrother_server_backend/packages/infrastructure/database/sqlite"
	"errors"
	"path/filepath"
	"strings"
	"testing"
)

func TestRulesUseActualPortAndRestrictDiscovery(t *testing.T) {
	err := syncRules(`C:\Program Files\O'Brien\server.exe`, 4443, 42069, true, func(script string) error {
		for _, want := range []string{"-LocalPort 4443", "-LocalPort 42069 -RemoteAddress LocalSubnet", "-Profile Any", `-Program 'C:\Program Files\O''Brien\server.exe'`, LegacyTCPRule, "Remove-NetFirewallRule", "$ErrorActionPreference = 'Stop'", "exit 1"} {
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
	if err := syncRules("server.exe", 1984, 42069, true, func(string) error { return failure }); !errors.Is(err, failure) {
		t.Fatalf("failure hidden: %v", err)
	}
	for _, port := range []int{0, -1, 65536} {
		if err := syncRules("server.exe", port, 42069, true, func(string) error { t.Fatal("invalid port executed"); return nil }); err == nil {
			t.Fatal("invalid port accepted")
		}
	}
}

func TestPublicProfileSurvivesRuleRecreation(t *testing.T) {
	db := sqlite.New(filepath.Join(t.TempDir(), "firewall.db"))
	if err := db.Connect(); err != nil {
		t.Fatal(err)
	}
	defer db.Disconnect()
	for _, value := range []string{"", "0", "1", "0"} {
		if value != "" {
			if err := db.SetSetting(settingsapplication.FirewallPublicEnabled, value); err != nil {
				t.Fatal(err)
			}
		}
		for restart := 0; restart < 2; restart++ {
			if err := db.Disconnect(); err != nil {
				t.Fatal(err)
			}
			if err := db.Connect(); err != nil {
				t.Fatal(err)
			}
			enabled, err := settingsapplication.PublicFirewallEnabled(db)
			if err != nil {
				t.Fatal(err)
			}
			want := "-Profile Any "
			if value == "0" {
				want = "-Profile Private,Domain "
			}
			err = syncRules("server.exe", 1984, 42069, enabled, func(script string) error {
				if strings.Count(script, want) != 2 {
					t.Fatalf("preference %q, restart %d: both rules must use %q: %s", value, restart, want, script)
				}
				return nil
			})
			if err != nil {
				t.Fatal(err)
			}
		}
	}
}
