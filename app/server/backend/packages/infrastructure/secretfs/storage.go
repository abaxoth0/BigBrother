package secretfs

import (
	"crypto/tls"
	"database/sql"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

func PrivateTree(path string) error {
	return filepath.WalkDir(path, func(name string, entry os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.Type()&os.ModeSymlink != 0 {
			return fmt.Errorf("refusing secret symlink: %s", name)
		}
		return restrict(name, entry.IsDir(), false)
	})
}

func PrivateDirectory(path string) error {
	if err := os.MkdirAll(path, 0700); err != nil {
		return err
	}
	return PrivateTree(path)
}

func privateExisting(path string) error {
	info, err := os.Lstat(path)
	if os.IsNotExist(err) {
		return nil
	}
	if err != nil {
		return err
	}
	if info.Mode()&os.ModeSymlink != 0 {
		return fmt.Errorf("refusing secret symlink: %s", path)
	}
	return restrict(path, info.IsDir(), false)
}

func copyPrivate(source, target string) error {
	if _, err := os.Stat(target); err == nil {
		return privateExisting(target)
	}
	if err := privateExisting(source); err != nil {
		return err
	}
	input, err := os.Open(source)
	if err != nil {
		return err
	}
	defer input.Close()
	output, err := os.OpenFile(target, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
	if err != nil {
		return err
	}
	if err := restrict(target, false, false); err != nil {
		output.Close()
		os.Remove(target)
		return err
	}
	_, copyErr := io.Copy(output, input)
	syncErr := output.Sync()
	closeErr := output.Close()
	if copyErr != nil || syncErr != nil || closeErr != nil {
		os.Remove(target)
		return fmt.Errorf("copy secret: %v %v %v", copyErr, syncErr, closeErr)
	}
	return nil
}

// Prepare moves active storage into a private child directory. VACUUM INTO
// takes a consistent snapshot including committed WAL contents; originals are
// retained with restricted ACLs and never used again by the updated backend.
func Prepare(legacy string) (string, error) {
	parent, err := filepath.Abs(filepath.Dir(legacy))
	if err != nil {
		return "", err
	}
	if parent == filepath.VolumeName(parent)+string(os.PathSeparator) {
		return "", fmt.Errorf("application directory required")
	}
	for _, root := range []string{os.Getenv("PROGRAMDATA"), os.Getenv("PROGRAMFILES"), os.Getenv("WINDIR"), os.Getenv("USERPROFILE")} {
		if root != "" && strings.EqualFold(filepath.Clean(root), parent) {
			return "", fmt.Errorf("refusing system/profile directory: %s", parent)
		}
	}
	info, err := os.Lstat(parent)
	if err != nil {
		return "", err
	}
	if !info.IsDir() || info.Mode()&os.ModeSymlink != 0 {
		return "", fmt.Errorf("trusted application directory required: %s", parent)
	}
	if err := restrict(parent, true, true); err != nil {
		return "", err
	}
	private := filepath.Join(parent, "data")
	if err := PrivateDirectory(private); err != nil {
		return "", err
	}
	target := filepath.Join(private, filepath.Base(legacy))
	for _, suffix := range []string{"", "-wal", "-shm", "-journal"} {
		if err := privateExisting(legacy + suffix); err != nil {
			return "", err
		}
	}
	if _, err := os.Stat(target); os.IsNotExist(err) {
		if _, err := os.Stat(legacy); err == nil {
			db, err := sql.Open("sqlite3", legacy)
			if err != nil {
				return "", err
			}
			staged, err := os.CreateTemp(private, ".migration-*.db")
			if err != nil {
				db.Close()
				return "", err
			}
			stagingPath := staged.Name()
			staged.Close()
			if err := privateExisting(stagingPath); err != nil {
				db.Close()
				os.Remove(stagingPath)
				return "", err
			}
			_, backupErr := db.Exec("VACUUM INTO ?", stagingPath)
			closeErr := db.Close()
			if backupErr != nil || closeErr != nil {
				os.Remove(stagingPath)
				return "", fmt.Errorf("database migration: %v %v", backupErr, closeErr)
			}
			staged, err = os.OpenFile(stagingPath, os.O_RDWR, 0)
			if err != nil {
				os.Remove(stagingPath)
				return "", err
			}
			syncErr := staged.Sync()
			closeErr = staged.Close()
			if syncErr != nil || closeErr != nil {
				os.Remove(stagingPath)
				return "", fmt.Errorf("flush migration: %v %v", syncErr, closeErr)
			}
			// Publish only a completed snapshot and never replace a database
			// another startup has already made active. Both names are private.
			linkErr := os.Link(stagingPath, target)
			os.Remove(stagingPath)
			if linkErr != nil && !os.IsExist(linkErr) {
				return "", linkErr
			}
		} else if !os.IsNotExist(err) {
			return "", err
		}
	} else if err != nil {
		return "", err
	}
	if err := privateExisting(target); err != nil {
		return "", err
	}
	oldTLS := filepath.Join(parent, "tls")
	newTLS := filepath.Join(private, "tls")
	if err := PrivateDirectory(newTLS); err != nil {
		return "", err
	}
	_, activeCertErr := os.Stat(filepath.Join(newTLS, "server.crt"))
	_, activeKeyErr := os.Stat(filepath.Join(newTLS, "server.key"))
	activeIdentity := activeCertErr == nil && activeKeyErr == nil
	if _, err := os.Stat(oldTLS); err == nil {
		if err := PrivateTree(oldTLS); err != nil {
			return "", err
		}
		cert, key := filepath.Join(oldTLS, "server.crt"), filepath.Join(oldTLS, "server.key")
		_, certErr := os.Stat(cert)
		_, keyErr := os.Stat(key)
		if !activeIdentity && (certErr == nil || keyErr == nil) {
			if _, err := tls.LoadX509KeyPair(cert, key); err != nil {
				return "", fmt.Errorf("existing TLS identity: %w", err)
			}
			if err := copyPrivate(cert, filepath.Join(newTLS, "server.crt")); err != nil {
				return "", err
			}
			if err := copyPrivate(key, filepath.Join(newTLS, "server.key")); err != nil {
				return "", err
			}
		}
	} else if !os.IsNotExist(err) {
		return "", err
	}
	cert, key := filepath.Join(newTLS, "server.crt"), filepath.Join(newTLS, "server.key")
	_, certErr := os.Stat(cert)
	_, keyErr := os.Stat(key)
	if certErr == nil || keyErr == nil {
		if _, err := tls.LoadX509KeyPair(cert, key); err != nil {
			return "", fmt.Errorf("private TLS identity: %w", err)
		}
	}
	return target, nil
}
