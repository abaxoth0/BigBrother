package app

import (
	"bigbrother_server_backend/packages/common/log"
	"os"
	"path"
)

func StartInit() {
	if err := log.DefaultLogger.AddForwarding(log.Stdout); err != nil {
		panic(err.Error())
	}
}

func EndInit() {
}

func InitDefaults() {
	// Ain logger's Init() requires the parent directory of config.Path to exist.
	// Create it here so Init() doesn't fail.
	parentDir := path.Dir(log.DefaultLoggerConfig.Path)
	if err := os.MkdirAll(parentDir, 0600); err != nil {
		appLogger.Fatal("Failed to create log parent directory", err.Error(), nil)
	}

	appLogger.Info("Initializing default logger...", nil)

	if err := log.DefaultLogger.Init(); err != nil {
		appLogger.Fatal("Failed to initalize default logger", err.Error(), nil)
	}

	appLogger.Info("Initializing default logger: OK", nil)
}
