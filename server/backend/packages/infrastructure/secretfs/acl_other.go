//go:build !windows

package secretfs

import "os"

func restrict(path string, directory, public bool) error {
	mode := os.FileMode(0600)
	if directory {
		mode = 0700
	}
	if public {
		mode = 0755
	}
	return os.Chmod(path, mode)
}
