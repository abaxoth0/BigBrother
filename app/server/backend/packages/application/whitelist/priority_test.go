package whitelistapplication

import (
	"bigbrother_server_backend/packages/domain/entity"
	"reflect"
	"testing"
)

func TestNormalizePriorities(t *testing.T) {
	for _, tc := range []struct {
		name        string
		input, want []string
	}{
		{"legacy", []string{"example.com", "!blocked.com"}, []string{"example.com", "!blocked.com"}},
		{"default", []string{"# comment", "", "[10]", "GitHub.com", "!gist.github.com", "[20]other.org", "[0]base.org"}, []string{"[10]github.com", "[10]!gist.github.com", "[20]other.org", "base.org"}},
		{"first rule prefix", []string{"[10]example.com", "other.org"}, []string{"[10]example.com", "other.org"}},
		{"zero", []string{"[0]", "[000]EXAMPLE.com", "[00010] !other.org:443"}, []string{"example.com", "[10]!other.org:443"}},
		{"only default", []string{"[10]", "# no rules"}, []string{}},
		{"max", []string{"[2147483647]example.com"}, []string{"[2147483647]example.com"}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			entries := []*entity.WhitelistEntry{}
			for _, text := range tc.input {
				entries = append(entries, &entity.WhitelistEntry{Value: text})
			}
			rules, err := NormalizePriorities(entries)
			if err != nil {
				t.Fatal(err)
			}
			got := []string{}
			for _, rule := range rules {
				got = append(got, rule.Value)
			}
			if !reflect.DeepEqual(got, tc.want) {
				t.Fatalf("got %v, want %v", got, tc.want)
			}
			for i, entry := range entries {
				if entry.Value != tc.input[i] {
					t.Fatal("normalization changed saved source text")
				}
			}
		})
	}
	for _, input := range [][]string{
		{"example.com", "[10]"}, {"[10]", "[20]"}, {"[]example.com"}, {"[default 10]"},
		{"[-1]example.com"}, {"[+1]example.com"}, {"[2147483648]example.com"},
		{"[99999999999999999999999]example.com"}, {"[10example.com"}, {"[1][2]example.com"}, {"![10]example.com"},
	} {
		entries := []*entity.WhitelistEntry{}
		for _, text := range input {
			entries = append(entries, &entity.WhitelistEntry{Value: text})
		}
		if _, err := NormalizePriorities(entries); err == nil {
			t.Fatalf("accepted malformed policy: %v", input)
		}
	}
}

func TestNormalizeInheritedDefaultsIndependently(t *testing.T) {
	entries := []*entity.WhitelistEntry{
		{WhitelistID: "child", Value: "[10]"}, {WhitelistID: "child", Value: "child.org"},
		{WhitelistID: "parent", Value: "[20]"}, {WhitelistID: "parent", Value: "parent.org"},
		{WhitelistID: "child", Value: "[0]base.org"},
	}
	got, err := NormalizePriorities(entries)
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 3 || got[0].Value != "[10]child.org" || got[1].Value != "[20]parent.org" || got[2].Value != "base.org" {
		t.Fatalf("scope leaked: %+v", got)
	}
}

func TestPriorityRevisionMatchesFirewallReadback(t *testing.T) {
	entries := []*entity.WhitelistEntry{
		{Value: "!github.com:443"},
		{Value: "[10]github.com:443"},
		{Value: "[20]!gist.github.com"},
	}
	if got := Revision(entries); got != "1726c205c31a04702c2a8c24b174e7d683ba7ac257a564e892010cb6afbabdee" {
		t.Fatalf("priority revision differs from client: %s", got)
	}
}
