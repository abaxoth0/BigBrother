package whitelistapplication

import (
	"bigbrother_server_backend/packages/domain/entity"
	"crypto/sha256"
	"encoding/hex"
	"slices"
	"sort"
	"strings"
)

func Revision(entries []*entity.WhitelistEntry) string {
	hash := sha256.New()
	for _, entry := range entries {
		hash.Write([]byte(entry.Value + "\n"))
	}
	return hex.EncodeToString(hash.Sum(nil))
}

func Compose(db QueryHandler) (*entity.WhitelistComposition, error) {
	whitelists, err := db.GetWhitelists()
	if err != nil {
		return nil, err
	}
	sort.Slice(whitelists, func(i, j int) bool { return whitelists[i].Name < whitelists[j].Name })
	type rule struct {
		entry   *entity.WhitelistEntry
		sources []string
	}
	rules := map[string]*rule{}
	for _, wl := range whitelists {
		if !wl.IsActive {
			continue
		}
		entries, err := db.GetWhitelistEntries(wl.Name)
		if err != nil {
			return nil, err
		}
		for _, entry := range entries {
			value := strings.ToLower(strings.TrimSpace(entry.Value))
			if value == "" || strings.HasPrefix(value, "#") || strings.HasPrefix(value, ";") {
				continue
			}
			r, ok := rules[value]
			if !ok {
				r = &rule{entry: &entity.WhitelistEntry{ID: entry.ID, Value: value}}
				rules[value] = r
			}
			if !slices.Contains(r.sources, wl.Name) {
				r.sources = append(r.sources, wl.Name)
			}
		}
	}
	values := make([]string, 0, len(rules))
	for value := range rules {
		values = append(values, value)
	}
	sort.Strings(values)
	result := &entity.WhitelistComposition{
		Entries: []*entity.WhitelistEntry{}, Conflicts: []entity.WhitelistConflict{},
	}
	for _, value := range values {
		r := rules[value]
		if strings.HasPrefix(value, "!") {
			if _, conflict := rules[value[1:]]; conflict {
				continue
			}
		} else if exception, conflict := rules["!"+value]; conflict {
			result.Conflicts = append(result.Conflicts, entity.WhitelistConflict{
				Pattern: value, AllowedBy: r.sources, ExcludedBy: exception.sources,
			})
			continue
		}
		r.entry.Sources = r.sources
		result.Entries = append(result.Entries, r.entry)
	}
	return result, nil
}
