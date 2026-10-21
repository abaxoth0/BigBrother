package notification

import (
	"strings"
	"unicode/utf8"
)

func notificationText(text string, limit int, fallback string) string {
	var output strings.Builder
	output.Grow(limit + utf8.UTFMax)
	truncated := false
	for _, char := range text {
		if char < 32 && char != '\t' && char != '\n' && char != '\r' {
			char = ' '
		}
		if char == '\ufffe' || char == '\uffff' {
			char = utf8.RuneError
		}
		if output.Len()+utf8.RuneLen(char) > limit {
			truncated = true
			break
		}
		output.WriteRune(char)
	}
	result := output.String()
	if strings.TrimSpace(result) == "" {
		result = fallback
	}
	if truncated {
		end := len(result)
		if limit >= len("…") {
			for end > limit-len("…") {
				_, size := utf8.DecodeLastRuneInString(result[:end])
				end -= size
			}
			result = result[:end] + "…"
		}
	}
	return result
}
