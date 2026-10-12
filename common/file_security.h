#ifndef BB_FILE_SECURITY_H
#define BB_FILE_SECURITY_H

#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <stdio.h>
#include <string.h>

static inline int bb_enable_restore_privilege(void) {
    HANDLE token;
    TOKEN_PRIVILEGES privileges = {0};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return -1;
    privileges.PrivilegeCount = 1;
    if (!LookupPrivilegeValueA(NULL, "SeRestorePrivilege", &privileges.Privileges[0].Luid)) {
        CloseHandle(token); return -1;
    }
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    BOOL ok = AdjustTokenPrivileges(token, FALSE, &privileges, 0, NULL, NULL);
    DWORD error = GetLastError();
    CloseHandle(token);
    return ok && error == ERROR_SUCCESS ? 0 : -1;
}

static inline int bb_file_acl(const char* path, int directory, int public_read) {
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return -1;
    if (bb_enable_restore_privilege() != 0) return -1;
    const char* sddl = public_read
        ? "O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)"
        : directory ? "O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)"
                    : "O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)";
    PSECURITY_DESCRIPTOR descriptor = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl, SDDL_REVISION_1, &descriptor, NULL)) return -1;
    PACL dacl; PSID owner; BOOL defaulted, present;
    int result = -1;
    if (GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) &&
        GetSecurityDescriptorOwner(descriptor, &owner, &defaulted)) {
        DWORD error = SetNamedSecurityInfoA((LPSTR)path, SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            owner, NULL, dacl, NULL);
        if (error == ERROR_SUCCESS) result = 0;
        else SetLastError(error);
    }
    LocalFree(descriptor);
    return result;
}

static inline int bb_private_tree(const char* path) {
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return -1;
    int directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (bb_file_acl(path, directory, 0) != 0) return -1;
    if (!directory) return 0;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA entry;
    HANDLE find = FindFirstFileA(pattern, &entry);
    if (find == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    int result = 0;
    do {
        if (!strcmp(entry.cFileName, ".") || !strcmp(entry.cFileName, "..")) continue;
        char child[MAX_PATH];
        int size = snprintf(child, sizeof(child), "%s\\%s", path, entry.cFileName);
        if (size < 0 || size >= MAX_PATH || bb_private_tree(child) != 0) { result = -1; break; }
    } while (FindNextFileA(find, &entry));
    FindClose(find);
    return result;
}

static inline int bb_secret_attributes(SECURITY_ATTRIBUTES* attributes) {
    memset(attributes, 0, sizeof(*attributes));
    attributes->nLength = sizeof(*attributes);
    return bb_enable_restore_privilege() == 0 && ConvertStringSecurityDescriptorToSecurityDescriptorA(
        "O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1,
        (PSECURITY_DESCRIPTOR*)&attributes->lpSecurityDescriptor, NULL) ? 0 : -1;
}
#endif
