package log

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/abaxoth0/Ain/logger"
)

var (
	Stdout = logger.NewStdOutLogger("")
	Stderr = logger.NewStdErrLogger("")
)

var DefaultLoggerConfig = &logger.FileLoggerConfig{
	Path: func() string {
		exe, err := os.Executable()
		if err == nil {
			return filepath.ToSlash(filepath.Join(filepath.Dir(exe), "logs", "server"))
		}
		return filepath.ToSlash(filepath.Join(".", "logs", "server"))
	}(),
	FilePerm: 0600, // Group and Other bits are ignored on windows
	LoggerConfig: &logger.LoggerConfig{
		ApplicationName: "big-brother-server",
	},
}

var DefaultLogger = func() *logger.FileLogger {
	log, err := logger.NewFileLogger(DefaultLoggerConfig)
	if err != nil {
		fmt.Println("Failed to setup default Logger:", err)
		os.Exit(1)
	}
	return log
}()
