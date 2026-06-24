#ifndef DNS_ETW_H
#define DNS_ETW_H

#include <windows.h>

int DnsEtwStart(HANDLE stop_event);
void DnsEtwStop(void);

#endif
