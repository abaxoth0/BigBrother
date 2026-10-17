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
		func(m *Manifest) { m.File = strings.Repeat("<", 1500) },
	} {
		m := valid
		change(&m)
		data, _ := json.Marshal(m)
		if _, err := Parse(string(data)); err == nil {
			t.Fatalf("invalid publication accepted: %s", data)
		}
	}
}
