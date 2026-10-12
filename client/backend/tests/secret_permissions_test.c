#include "../../../common/file_security.h"
#include <assert.h>

static void metadata(const char* path) {
    PSECURITY_DESCRIPTOR descriptor;
    PSID owner; PACL dacl;
    assert(GetNamedSecurityInfoA((LPSTR)path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                &owner, NULL, &dacl, NULL, &descriptor) == ERROR_SUCCESS);
    char* owner_text;
    assert(ConvertSidToStringSidA(owner, &owner_text));
    assert(strcmp(owner_text, "S-1-5-32-544") == 0);
    SECURITY_DESCRIPTOR_CONTROL control; DWORD revision;
    assert(GetSecurityDescriptorControl(descriptor, &control, &revision));
    assert(control & SE_DACL_PROTECTED);
    assert(dacl && dacl->AceCount == 2);
    LocalFree(owner_text); LocalFree(descriptor);
}

int main(void) {
    if (GetProcAddress(GetModuleHandleA("ntdll.dll"),"wine_get_version")) {
        puts("SKIPPED: Windows ownership and NTFS access enforcement require the Windows VM.");
        return 0;
    }
    char root[MAX_PATH], directory[MAX_PATH], file[MAX_PATH];
    GetModuleFileNameA(NULL, root, sizeof(root));
    char* slash = strrchr(root, '\\'); assert(slash); *slash = '\0';
    assert(bb_file_acl(root,1,1) == 0);
    snprintf(directory,sizeof(directory),"%s\\private",root);
    CreateDirectoryA(directory,NULL);
    assert(bb_private_tree(directory) == 0);
    snprintf(file,sizeof(file),"%s\\test.key",directory);
    SECURITY_ATTRIBUTES attributes;
    assert(bb_secret_attributes(&attributes) == 0);
    HANDLE output = CreateFileA(file,GENERIC_WRITE,0,&attributes,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    LocalFree(attributes.lpSecurityDescriptor);
    assert(output != INVALID_HANDLE_VALUE);
    DWORD written;
    assert(WriteFile(output,"test-secret",11,&written,NULL));
    CloseHandle(output);
    assert(bb_file_acl(file,0,0) == 0);
    metadata(directory); metadata(file);
    BYTE admins[SECURITY_MAX_SID_SIZE], users[SECURITY_MAX_SID_SIZE];
    DWORD size = sizeof(admins); assert(CreateWellKnownSid(WinBuiltinAdministratorsSid,NULL,admins,&size));
    size = sizeof(users); assert(CreateWellKnownSid(WinBuiltinUsersSid,NULL,users,&size));
    SID_AND_ATTRIBUTES disabled = {admins,0}, restricted = {users,0};
    HANDLE original, limited;
    assert(OpenProcessToken(GetCurrentProcess(),TOKEN_DUPLICATE|TOKEN_QUERY,&original));
    assert(CreateRestrictedToken(original,DISABLE_MAX_PRIVILEGE,1,&disabled,0,NULL,1,&restricted,&limited));
    assert(ImpersonateLoggedOnUser(limited));
    for (int i = 0; i < 5; i++) {
        DWORD access[] = {GENERIC_READ,GENERIC_WRITE,DELETE,WRITE_DAC,WRITE_OWNER};
        HANDLE handle = CreateFileA(file,access[i],FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
        assert(handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED);
    }
    assert(!DeleteFileA(file) && GetLastError() == ERROR_ACCESS_DENIED);
    HANDLE parent = CreateFileA(directory,DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,
                                OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    assert(parent == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED);
    assert(RevertToSelf());
    CloseHandle(limited); CloseHandle(original);
    puts("Secret permission metadata and restricted-token access-denial tests passed.");
    return 0;
}
