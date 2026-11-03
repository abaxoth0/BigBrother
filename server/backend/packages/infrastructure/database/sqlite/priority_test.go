package sqlite

import (
	whitelistapplication "bigbrother_server_backend/packages/application/whitelist"
	"bigbrother_server_backend/packages/domain/entity"
	"reflect"
	"testing"
	"time"
)

func TestTemporaryWhitelistPriorityCompositionAndExpiry(t *testing.T) {
	db, advance := timerDatabase(t)
	mustTimer(t, db.CreateWhitelist("base", ""))
	mustTimer(t, db.ReplaceWhitelistEntries([]string{"[0]", "!\"github\"", "!github.com:443", "example.org"}, "base"))
	mustTimer(t, db.SetWhitelistActive("base", true))
	mustTimer(t, db.CreateTemporaryWhitelist("temporary", 60, true))
	source := []string{"# grant", "[10]", "*.github.com:443", "github.com:443", "[20]!gist.github.com", "[0]example.org"}
	mustTimer(t, db.ReplaceWhitelistEntries(source, "temporary"))
	mustTimer(t, db.SetWhitelistActive("temporary", true))
	result, err := whitelistapplication.Compose(db)
	mustTimer(t, err)
	got := []string{}
	for _, entry := range result.Entries {
		got = append(got, entry.Value)
	}
	want := []string{"!\"github\"", "!github.com:443", "[10]*.github.com:443", "[10]github.com:443", "[20]!gist.github.com", "example.org"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("composed priority rules: %v, want %v", got, want)
	}
	conflicts := []entity.WhitelistConflict{{Pattern: "github.com:443", AllowedBy: []string{"temporary"}, ExcludedBy: []string{"base"}, AllowedPriority: 10}}
	if !reflect.DeepEqual(result.Conflicts, conflicts) {
		t.Fatalf("priority conflict report: %+v", result.Conflicts)
	}
	if !reflect.DeepEqual(result.Entries[len(result.Entries)-1].Sources, []string{"base", "temporary"}) {
		t.Fatal("equivalent explicit-zero rules were not deduplicated")
	}
	revision := whitelistapplication.Revision(result.Entries)
	saved, err := db.GetWhitelistEntries("temporary")
	mustTimer(t, err)
	for i, entry := range saved {
		if entry.Value != source[i] {
			t.Fatalf("source order changed: %+v", saved)
		}
	}
	advance(time.Minute)
	changed, err := db.ExpireWhitelistTimers()
	mustTimer(t, err)
	if !changed {
		t.Fatal("expiry did not notify clients")
	}
	result, err = whitelistapplication.Compose(db)
	mustTimer(t, err)
	if len(result.Entries) != 3 || len(result.Conflicts) != 0 || whitelistapplication.Revision(result.Entries) == revision {
		t.Fatalf("temporary priority grant survived expiry: %+v", result)
	}
	for _, entry := range result.Entries {
		if entry.Value[0] == '[' {
			t.Fatalf("expired override retained: %s", entry.Value)
		}
	}
}

func TestWhitelistPriorityOrderSurvivesRestart(t *testing.T) {
	db := newTestDatabase(t)
	mustTimer(t, db.CreateWhitelist("ordered", ""))
	source := []string{"# first", "[10]", "z.example", "a.example", "[0]b.example"}
	mustTimer(t, db.ReplaceWhitelistEntries(source, "ordered"))
	mustTimer(t, db.AddWhitelistEntry("last.example", "ordered"))
	source = append(source, "last.example")
	mustTimer(t, db.Disconnect())
	mustTimer(t, db.Connect())
	entries, err := db.GetWhitelistEntries("ordered")
	mustTimer(t, err)
	got := []string{}
	for _, entry := range entries {
		got = append(got, entry.Value)
	}
	if !reflect.DeepEqual(got, source) {
		t.Fatalf("entry order: %v, want %v", got, source)
	}
}

func TestInheritedWhitelistPriorityScope(t *testing.T) {
	db := newTestDatabase(t)
	mustTimer(t, db.CreateWhitelist("parent", ""))
	parentID, err := db.GetWhitelistID("parent")
	mustTimer(t, err)
	mustTimer(t, db.CreateWhitelist("child", parentID))
	mustTimer(t, db.ReplaceWhitelistEntries([]string{"[20]", "parent.org"}, "parent"))
	mustTimer(t, db.ReplaceWhitelistEntries([]string{"[10]", "child.org"}, "child"))
	mustTimer(t, db.SetWhitelistActive("child", true))
	result, err := whitelistapplication.Compose(db)
	mustTimer(t, err)
	if len(result.Entries) != 2 || result.Entries[0].Value != "[10]child.org" || result.Entries[1].Value != "[20]parent.org" {
		t.Fatalf("inherited default leaked: %+v", result.Entries)
	}
}
