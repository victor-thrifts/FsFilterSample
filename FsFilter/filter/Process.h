#ifndef __MSPROCESS_H__
#define __MSPROCESS_H__

/*************************************************************************
    Prototypes
*************************************************************************/

NTSTATUS GetCurrentProcessName();
NTSTATUS GetSID(__deref_out PUNICODE_STRING sidString, PACCESS_STATE AccessState);
NTSTATUS GetProcessImageName(HANDLE processId, PUNICODE_STRING ProcessImageName);


#endif  //__MSPROCESS_H__