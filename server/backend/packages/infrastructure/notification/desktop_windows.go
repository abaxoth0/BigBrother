package notification

import (
	"context"
	"encoding/hex"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
	"time"
)

func DeliverWindowsNotification(parent context.Context, title, body string) error {
	executable, err := os.Executable()
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(parent, 10*time.Second)
	defer cancel()
	command := exec.CommandContext(ctx, filepath.Join(filepath.Dir(executable), "BigBrother Notifications.exe"),
		"--send", "Server", hex.EncodeToString([]byte(title)), hex.EncodeToString([]byte(body)))
	command.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
	output, err := command.CombinedOutput()
	if err != nil {
		return fmt.Errorf("Windows toast delivery failed: %w: %s", err, output)
	}
	return nil
}
