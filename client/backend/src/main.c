/**
 * @file main.c
 * @brief BigBrother Client - CLI for testing IPC to Daemon.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/ipc.h"

static void print_usage(const char* prog) {
    printf("Usage: %s <command>\n", prog);
    printf("\nCommands:\n");
    printf("  status      - Get daemon status\n");
    printf("  whitelist   - Get current whitelist\n");
    printf("  reload      - Reload whitelist from file\n");
    printf("  set <file> - Set whitelist from file\n");
}

static void print_response(const char* cmd, int result, const char* response) {
    if (result != 0) {
        printf("[%s] Error: could not connect to daemon\n", cmd);
        return;
    }
    
    printf("[%s] Response:\n%s\n", cmd, response);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    char buffer[4096];
    
    if (strcmp(argv[1], "status") == 0) {
        int result = IpcGetStatus(buffer, sizeof(buffer));
        print_response("status", result, buffer);
        
    } else if (strcmp(argv[1], "whitelist") == 0) {
        int result = IpcGetWhitelist(buffer, sizeof(buffer));
        print_response("whitelist", result, buffer);
        
    } else if (strcmp(argv[1], "reload") == 0) {
        int result = IpcReload(buffer, sizeof(buffer));
        print_response("reload", result, buffer);
        
    } else if (strcmp(argv[1], "set") == 0) {
        if (argc < 3) {
            printf("Error: specify file\n");
            return 1;
        }
        
        FILE* f = fopen(argv[2], "rb");
        if (!f) {
            printf("Error: could not open file '%s'\n", argv[2]);
            return 1;
        }
        
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        char* data = malloc(size + 1);
        fread(data, 1, size, f);
        data[size] = '\0';
        fclose(f);
        
        int result = IpcSetWhitelist(data, size, buffer, sizeof(buffer));
        print_response("set", result, buffer);
        free(data);
        
    } else {
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
