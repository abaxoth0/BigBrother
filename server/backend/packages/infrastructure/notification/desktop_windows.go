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
	return deliverWindowsNotification(parent, filepath.Join(filepath.Dir(executable), "BigBrother Notifications.exe"), title, body)
}

func deliverWindowsNotification(parent context.Context, helper, title, body string) error {
	ctx, cancel := context.WithTimeout(parent, 30*time.Second)
	defer cancel()
	command := exec.CommandContext(ctx, helper,
		"--send", "Server", hex.EncodeToString([]byte(notificationText(title, 256, "BigBrother"))), hex.EncodeToString([]byte(notificationText(body, 1024, "Notification"))))
	command.WaitDelay = time.Second
	command.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
	output, err := command.CombinedOutput()
	if err != nil {
		if ctx.Err() != nil {
			err = ctx.Err()
		}
		return fmt.Errorf("Windows toast delivery failed: %w: %s", err, output)
	}
	return nil
}
