package updates

import (
	"encoding/json"
	"strings"
	"testing"
)

func TestPublicationMetadata(t *testing.T) {
	valid := Manifest{Version: "1.4.0", Location: `Z:\shared\tmp\BigBrother Updater.exe`, Size: 4096, SHA256: strings.Repeat("a", 64)}
	for _, path := range []string{valid.Location, `\\server\share\BigBrother Updater.exe`} {
		m := valid
		m.Location = path
		data, _ := json.Marshal(m)
		got, err := Parse(string(data))
		if err != nil || got.Location != path {
			t.Fatalf("valid client path rejected: %s, %v", path, err)
		}
	}
	for _, change := range []func(*Manifest){
		func(m *Manifest) { m.Location = "relative.exe" },
		func(m *Manifest) { m.Location = `Z:relative.exe` },
		func(m *Manifest) { m.Location = "https://host/update.exe" },
		func(m *Manifest) { m.Location = "Z:\\bad\n.exe" },
		func(m *Manifest) { m.Location = `Z:\update.cmd` },
		func(m *Manifest) { m.Version = "1.4.0-beta" },
		func(m *Manifest) { m.SHA256 = "bad" },
		func(m *Manifest) { m.Size = MaxSize + 1 },
		func(m *Manifest) { m.File = strings.Repeat("<", 30000) },
		func(m *Manifest) {
			m.ReleaseNotes = &ReleaseNotes{Version: "1.4.0", Sections: []ReleaseSection{{Kind: "unknown", Items: []string{"entry"}}}}
		},
		func(m *Manifest) {
			m.ReleaseNotes = &ReleaseNotes{Version: "1.4.0", Sections: []ReleaseSection{{Kind: "fixed", Items: []string{""}}}}
		},
	} {
		m := valid
		change(&m)
		data, _ := json.Marshal(m)
		if _, err := Parse(string(data)); err == nil {
			t.Fatalf("invalid publication accepted: %s", data)
		}
	}
}

func TestReleaseNotesRoundTrip(t *testing.T) {
	expected := Manifest{Version: "1.4.0", Location: `Z:\update.exe`, Size: 4096, SHA256: strings.Repeat("a", 64),
		ReleaseNotes: &ReleaseNotes{Version: "Unreleased", Sections: []ReleaseSection{{Kind: "changed", Items: []string{strings.Repeat("Описание изменений. ", 1000)}}}}}
	data, _ := json.Marshal(expected)
	got, err := Parse(string(data))
	if err != nil {
		t.Fatal(err)
	}
	if got.ReleaseNotes.Sections[0].Items[0] != expected.ReleaseNotes.Sections[0].Items[0] {
		t.Fatal("release notes lost during parsing")
	}
	got.ReleaseNotes.Sections[0].Items[0] = strings.Repeat("a", MaxMetadataSize)
	if err := got.Validate(); err == nil {
		t.Fatal("oversized notes accepted")
	}
}
