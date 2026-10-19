package log

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/abaxoth0/Ain/logger"
)

func TestBuildLoggingLevels(t *testing.T) {
	config := *DefaultLoggerConfig
	config.Path = t.TempDir()
	output, err := logger.NewFileLogger(&config)
	if err != nil {
		t.Fatal(err)
	}
	if err := output.Init(); err != nil {
		t.Fatal(err)
	}
	if err := output.Start(); err != nil {
		t.Fatal(err)
	}
	source := logger.NewSource("BUILD_TEST", output)
	source.Info("info-probe", nil)
	source.Debug("debug-probe", nil)
	source.Trace("trace-probe", nil)
	source.Info("finished-probe", nil)
	deadline := time.Now().Add(2 * time.Second)
	for {
		files, err := os.ReadDir(config.Path)
		if err != nil || len(files) != 1 {
			t.Fatalf("expected one log file: %v, %v", files, err)
		}
		data, err := os.ReadFile(filepath.Join(config.Path, files[0].Name()))
		if err != nil {
			t.Fatal(err)
		}
		if strings.Contains(string(data), "finished-probe") {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("logger did not finish writing entries")
		}
		time.Sleep(time.Millisecond)
	}
	if err := output.Stop(true); err != nil {
		t.Fatal(err)
	}

	files, err := os.ReadDir(config.Path)
	if err != nil {
		t.Fatal(err)
	}
	var content strings.Builder
	for _, file := range files {
		data, err := os.ReadFile(filepath.Join(config.Path, file.Name()))
		if err != nil {
			t.Fatal(err)
		}
		content.Write(data)
	}
	if !strings.Contains(content.String(), "info-probe") {
		t.Fatal("information log was suppressed")
	}
	for _, probe := range []string{"debug-probe", "trace-probe"} {
		if strings.Contains(content.String(), probe) != DiagnosticsEnabled {
			t.Errorf("%s visibility does not match the build configuration", probe)
		}
	}
}
