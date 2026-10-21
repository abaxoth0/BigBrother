package notification

import (
	"context"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"strings"
	"testing"
	"time"
	"unicode/utf8"
)

func TestMain(m *testing.M) {
	if len(os.Args) == 5 && os.Args[1] == "--send" {
		title, err := hex.DecodeString(os.Args[3])
		if err != nil {
			os.Exit(2)
		}
		body, err := hex.DecodeString(os.Args[4])
		if err != nil || !utf8.Valid(body) || len(body) > 1024 || strings.ContainsRune(string(body), 0) {
			os.Exit(2)
		}
		switch string(title) {
		case "FAIL":
			fmt.Fprint(os.Stderr, "User session 1: simulated WinRT failure")
			os.Exit(5)
		case "WAIT":
			time.Sleep(time.Minute)
		}
		os.Exit(0)
	}
	os.Exit(m.Run())
}

func TestWindowsNotificationReportsChildDetails(t *testing.T) {
	executable, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	err = deliverWindowsNotification(context.Background(), executable, "FAIL", "body")
	if err == nil || !strings.Contains(err.Error(), "simulated WinRT failure") {
		t.Fatalf("missing child diagnostics: %v", err)
	}
}

func TestWindowsNotificationCancellation(t *testing.T) {
	executable, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
	defer cancel()
	started := time.Now()
	err = deliverWindowsNotification(ctx, executable, "WAIT", "body")
	if !errors.Is(err, context.DeadlineExceeded) || time.Since(started) > 3*time.Second {
		t.Fatalf("delivery did not respect cancellation: %v", err)
	}
}

func TestWindowsNotificationPreparesPayload(t *testing.T) {
	executable, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	if err := deliverWindowsNotification(context.Background(), executable, "VERIFY", "\xff\x00"+strings.Repeat("😀", 1000)); err != nil {
		t.Fatal(err)
	}
}
