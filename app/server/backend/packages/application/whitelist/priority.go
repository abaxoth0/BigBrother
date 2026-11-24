package whitelistapplication

import (
	"bigbrother_server_backend/packages/application/rules"
	"bigbrother_server_backend/packages/domain/entity"
	"fmt"
)

func NormalizePriorities(entries []*entity.WhitelistEntry) ([]*entity.WhitelistEntry, error) {
	sources := map[string]*rules.Source{}
	result := make([]*entity.WhitelistEntry, 0, len(entries))
	for i, entry := range entries {
		source := sources[entry.WhitelistID]
		if source == nil {
			source = &rules.Source{}
			sources[entry.WhitelistID] = source
		}
		priority, body, err := source.Next(entry.Value)
		if err != nil {
			return nil, fmt.Errorf("line %d: %w", i+1, err)
		}
		if body == "" {
			continue
		}
		normalized := *entry
		normalized.Value = rules.FormatPriority(priority, body)
		result = append(result, &normalized)
	}
	return result, nil
}
