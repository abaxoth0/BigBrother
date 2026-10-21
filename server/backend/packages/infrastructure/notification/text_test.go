package notification

import (
	"strings"
	"testing"
	"unicode/utf8"
)

func TestNotificationText(t *testing.T) {
	for _, test := range []struct {
		input string
		limit int
		want  string
	}{
		{"Русский текст 😀 & <xml>", 256, "Русский текст 😀 & <xml>"},
		{"a\x00b\x01", 256, "a b "},
		{"\xff", 256, "�"},
		{"\ufffe\uffff", 256, "��"},
		{" \t", 256, "fallback"},
		{"😀😀😀", 10, "😀…"},
		{strings.Repeat("x", 2000), 1024, strings.Repeat("x", 1021) + "…"},
	} {
		got := notificationText(test.input, test.limit, "fallback")
		if got != test.want || !utf8.ValidString(got) || len(got) > test.limit {
			t.Errorf("input %q: got %q, want %q", test.input, got, test.want)
		}
	}
}
