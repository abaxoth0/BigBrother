package processlist

import (
	"slices"
	"strings"
	"testing"
)

func TestProcessRules(t *testing.T) {
	got, err := Compile("# apps\n[8]\nИгра*.EXE\n[10]!Игра Editor.exe\n[0]Browser.exe")
	if err != nil || !slices.Equal(got, []string{"[8]игра*.exe", "[10]!игра editor.exe", "browser.exe"}) {
		t.Fatal(got, err)
	}
	for _, bad := range []string{"*.exe", "game.exe\n[10]", "a?.exe", "c:\\game.exe", "game.exe:80", "foo*bar.exe", "!", "\x00bad.exe", strings.Repeat("x", 8193)} {
		if _, err := Compile(bad); err == nil {
			t.Errorf("accepted %q", bad)
		}
	}
	if empty, err := Compile("; comment"); err != nil || len(empty) != 0 {
		t.Fatal(empty, err)
	}
}
