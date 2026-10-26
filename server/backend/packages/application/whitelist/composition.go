package whitelistapplication

import (
	"bigbrother_server_backend/packages/domain/entity"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
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
		entry    *entity.WhitelistEntry
		sources  []string
		priority int
		body     string
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
		entries, err = NormalizePriorities(entries)
		if err != nil {
			return nil, fmt.Errorf("whitelist %q: %w", wl.Name, err)
		}
		for _, entry := range entries {
			value := entry.Value
			r, ok := rules[value]
			if !ok {
				priority, body, _, _ := ReadPriority(value)
				r = &rule{entry: &entity.WhitelistEntry{ID: entry.ID, Value: value}, priority: priority, body: body}
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
	allows, denies := map[string]*rule{}, map[string]*rule{}
	for _, value := range values {
		r := rules[value]
		r.entry.Sources = r.sources
		result.Entries = append(result.Entries, r.entry)
		body, target := r.body, allows
		if strings.HasPrefix(body, "!") {
			body, target = body[1:], denies
		}
		if previous, ok := target[body]; !ok || r.priority > previous.priority {
			target[body] = r
		}
	}
	patterns := make([]string, 0, len(allows))
	for pattern := range allows {
		patterns = append(patterns, pattern)
	}
	sort.Strings(patterns)
	for _, pattern := range patterns {
		allow := allows[pattern]
		if deny, ok := denies[pattern]; ok {
			result.Conflicts = append(result.Conflicts, entity.WhitelistConflict{
				Pattern: pattern, AllowedBy: allow.sources, ExcludedBy: deny.sources,
				AllowedPriority: allow.priority, ExcludedPriority: deny.priority,
			})
		}
	}
	return result, nil
}
