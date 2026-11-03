//go:build windows

package secretfs

import (
	"fmt"
	"golang.org/x/sys/windows"
	"sync"
)

var privilegeOnce sync.Once
var privilegeError error

func restrict(path string, directory, public bool) error {
	privilegeOnce.Do(func() {
		var token windows.Token
		privilegeError = windows.OpenProcessToken(windows.CurrentProcess(), windows.TOKEN_ADJUST_PRIVILEGES|windows.TOKEN_QUERY, &token)
		if privilegeError != nil {
			return
		}
		defer token.Close()
		name, _ := windows.UTF16PtrFromString("SeRestorePrivilege")
		var privileges windows.Tokenprivileges
		privileges.PrivilegeCount = 1
		privilegeError = windows.LookupPrivilegeValue(nil, name, &privileges.Privileges[0].Luid)
		if privilegeError != nil {
			return
		}
		privileges.Privileges[0].Attributes = windows.SE_PRIVILEGE_ENABLED
		privilegeError = windows.AdjustTokenPrivileges(token, false, &privileges, 0, nil, nil)
	})
	if privilegeError != nil {
		return privilegeError
	}
	flags := ""
	if directory {
		flags = "OICI"
	}
	sddl := "O:BAD:P(A;" + flags + ";FA;;;SY)(A;" + flags + ";FA;;;BA)"
	if public {
		sddl += "(A;" + flags + ";FRFX;;;BU)"
	}
	sd, err := windows.SecurityDescriptorFromString(sddl)
	if err != nil {
		return err
	}
	owner, _, err := sd.Owner()
	if err != nil {
		return err
	}
	dacl, _, err := sd.DACL()
	if err != nil {
		return err
	}
	if err := windows.SetNamedSecurityInfo(path, windows.SE_FILE_OBJECT,
		windows.OWNER_SECURITY_INFORMATION|windows.DACL_SECURITY_INFORMATION|windows.PROTECTED_DACL_SECURITY_INFORMATION,
		owner, nil, dacl, nil); err != nil {
		return fmt.Errorf("secure %s (run elevated or as SYSTEM): %w", path, err)
	}
	return nil
}
