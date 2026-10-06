#ifndef BB_PIPE_SECURITY_H
#define BB_PIPE_SECURITY_H

#include <windows.h>
#include <sddl.h>

/* Builds SECURITY_ATTRIBUTES that restrict a named pipe to SYSTEM, Builtin
 * Administrators, and the object owner. Use this for daemon IPC pipes so other
 * local processes can't drive the service. Returns 0 on success; the caller
 * must LocalFree() sa->lpSecurityDescriptor when the attributes are done with. */
static inline int bb_restrict_pipe_security(SECURITY_ATTRIBUTES* sa) {
    if (!sa) return -1;
    memset(sa, 0, sizeof(*sa));
    sa->nLength = sizeof(*sa);
    sa->bInheritHandle = FALSE;
    const char* sddl = "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
            sddl, SDDL_REVISION_1,
            (PSECURITY_DESCRIPTOR*)&sa->lpSecurityDescriptor, NULL)) {
        return -1;
    }
    return 0;
}

#endif /* BB_PIPE_SECURITY_H */