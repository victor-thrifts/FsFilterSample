#include <fltKernel.h>
//#include <dontuse.h>
#include <suppress.h>
#include <Ntstrsafe.h>

#include "conf.h"
#include "Process.h"


#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, GetProcessImageName)
#endif


typedef NTSTATUS (*QUERY_INFO_PROCESS) (
	__in HANDLE ProcessHandle,
	__in PROCESSINFOCLASS ProcessInformationClass,
	__out_bcount(ProcessInformationLength) PVOID ProcessInformation,
	__in ULONG ProcessInformationLength,
	__out_opt PULONG ReturnLength
);

QUERY_INFO_PROCESS ZwQueryInformationProcess;

NTKERNELAPI
UCHAR *
PsGetProcessImageFileName(
    __in PEPROCESS Process
); 

NTSTATUS GetCurrentProcessName()
{
	WCHAR strBuffer[(sizeof(UNICODE_STRING) + MAX_PATH*2)/sizeof(WCHAR)];
	PUNICODE_STRING str = (PUNICODE_STRING)strBuffer;
    PEPROCESS proc;
    NTSTATUS status;


	if (NULL == ZwQueryInformationProcess) {

		UNICODE_STRING routineName;

		RtlInitUnicodeString(&routineName, L"ZwQueryInformationProcess");

		ZwQueryInformationProcess =
			   (QUERY_INFO_PROCESS) MmGetSystemRoutineAddress(&routineName);

		if (NULL == ZwQueryInformationProcess) {
			DbgPrint("Cannot resolve ZwQueryInformationProcess\n");
		}
	}

	proc = PsGetCurrentProcess();
	DbgPrint("ZwOpenFile Called...\n");
	DbgPrint("PID: %d\n", PsGetProcessId(proc));
	DbgPrint("ImageFileName: %.16s\n", PsGetProcessImageFileName(proc));

	//initialize
	str->Length = 0x0;
	str->MaximumLength = MAX_PATH*2;
	str->Buffer = &strBuffer[sizeof(UNICODE_STRING) / sizeof(WCHAR)];


	//note that the seconds arg (27) is ProcessImageFileName
	status = ZwQueryInformationProcess(proc, 27, strBuffer, sizeof(strBuffer), NULL);
    if(status == STATUS_SUCCESS){
        DbgPrint("FullPath: %wZ\n", str->Buffer);
    }else{
        DbgPrint("ZwQueryInformationProcess failed! \n");
    }
	return status;
}


NTSTATUS GetProcessImageName(HANDLE processId, PUNICODE_STRING ProcessImageName)
{
	NTSTATUS status;
	ULONG returnedLength;
	ULONG bufferLength;
	HANDLE hProcess = NULL;
	PEPROCESS eProcess;

	PAGED_CODE(); // this eliminates the possibility of the IDLE Thread/Process

	status = PsLookupProcessByProcessId(processId, &eProcess);

	if(NT_SUCCESS(status))
	{
		status = ObOpenObjectByPointer(eProcess, 0, NULL, 0, 0, KernelMode, &hProcess);
		if(NT_SUCCESS(status))
		{
		} else {
			DbgPrint("ObOpenObjectByPointer Failed: %08x\n", status);
		}
		ObDereferenceObject(eProcess);
	} else {
		DbgPrint("PsLookupProcessByProcessId Failed: %08x\n", status);
	}


	if (NULL == ZwQueryInformationProcess) {

		UNICODE_STRING routineName;

		RtlInitUnicodeString(&routineName, L"ZwQueryInformationProcess");

		ZwQueryInformationProcess =
			   (QUERY_INFO_PROCESS) MmGetSystemRoutineAddress(&routineName);

		if (NULL == ZwQueryInformationProcess) {
			DbgPrint("Cannot resolve ZwQueryInformationProcess\n");
		}
	}

	/* Query the actual size of the process path */
	status = ZwQueryInformationProcess( hProcess,
										ProcessImageFileName,
										ProcessImageName, // buffer
										sizeof(UNICODE_STRING) + MAX_PATH*2, // buffer size
										&returnedLength);

	if (STATUS_INFO_LENGTH_MISMATCH == status) {
		return STATUS_INFO_LENGTH_MISMATCH;
	}

	/* Check there is enough space to store the actual process
	   path when it is found. If not return an error with the
	   required size */
	bufferLength = returnedLength - sizeof(UNICODE_STRING);

	if(bufferLength == 0) return STATUS_UNSUCCESSFUL;

	if (ProcessImageName->MaximumLength < bufferLength)
	{
		ProcessImageName->MaximumLength = (USHORT) bufferLength;
		return STATUS_BUFFER_OVERFLOW;   
	}

	if (NT_SUCCESS(status)) 
	{
		DbgPrint("The ProcessID: %d is %ws\n",processId, ProcessImageName->Buffer);
	}

	return status;
}


NTSTATUS GetCurrentThreadImageName(PFLT_CALLBACK_DATA Data,  PUNICODE_STRING ProcessImageName)
{
	PEPROCESS objCurProcess=NULL;
	HANDLE hProcess;
	UNICODE_STRING fullPath;

	UNREFERENCED_PARAMETER(ProcessImageName);

	objCurProcess=IoThreadToProcess(Data->Thread);//Note: Date is type of FLT_CALLBACK_DATA which is in PreOperation Callback as argument

	hProcess=PsGetProcessId(objCurProcess);

	fullPath.Length=0;
	fullPath.MaximumLength=520;
	fullPath.Buffer=(PWSTR)ExAllocatePoolWithTag(NonPagedPool,520,'uUT1');

	GetProcessImageName(hProcess,&fullPath);
	
	ExFreePoolWithTag(fullPath.Buffer, 'uUT1');
	fullPath.Buffer = NULL;
	fullPath.Length = 0;
	return STATUS_SUCCESS;
}


UNICODE_STRING *GetProcessUsername()
{
	HANDLE token;
	TOKEN_USER* TokenUser = NULL;
	NTSTATUS status;
	//PULONG len;
	//int index;
	//SID *sid;
	LUID luid;
	UNICODE_STRING userName;
	PSecurityUserData userInformation = NULL;
	status = ZwOpenProcessTokenEx(NtCurrentProcess(), GENERIC_READ, OBJ_KERNEL_HANDLE, &token);
	//ZwQueryInformationToken(token, (TOKEN_INFORMATION_CLASS)TokenUser, NULL, 0, &len); //to get required length
	if (!NT_SUCCESS(status))
	{
		KdPrint(("ZwOpenProcessTokenEx(): ZwOpenProcessTokenEx fail\n"));
		return NULL;
	}

	//sid= (SID*)TokenUser->User.Sid;
	// 

	status = SeQueryAuthenticationIdToken(token, &luid);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("SeQueryAuthenticationIdToken(): SeQueryAuthenticationIdToken fail\n"));
		ZwClose(token);
		return NULL;
	}
	ZwClose(token);
	status = STATUS_UNSUCCESSFUL;
	// status = GetSecurityUserInfo(&luid, UNDERSTANDS_LONG_NAMES, &userInformation);   //seckedd.dll required
	if (!NT_SUCCESS(status))
	{
		KdPrint(("GetSecurityUserInfo(): GetSecurityUserInfo fail\n"));
		return NULL;
	}
	userName.Length = 0;
	userName.MaximumLength = userInformation->UserName.Length;
	userName.Buffer = ExAllocatePool(NonPagedPool, userName.MaximumLength);
	if (userName.Buffer == NULL)
	{
		KdPrint(("GetUserName(): ExAllocatePool fail\n"));
		return NULL;
	}

	RtlCopyUnicodeString(&userName, &userInformation->UserName);
	return &userName;
}


NTSTATUS GetSID(PUNICODE_STRING sidString, PACCESS_STATE AccessState)
{
	NTSTATUS ntStatus;
	PVOID Token;
	HANDLE tokenHandle;
	PTOKEN_USER tokenInfoBuffer;
	ULONG requiredLength;
	PWCHAR sidStringBuffer;
	UNICODE_STRING nameString;
	UNICODE_STRING domainString;
	WCHAR* nameBuffer;
	WCHAR* domainBuffer;
	SID_NAME_USE nameUse = SidTypeUser;
	ULONG nameSize = 0;
	ULONG domainSize = 0;
	ULONG SIDLength;
	TOKEN_USER tokenuser;
	SID_AND_ATTRIBUTES tokenuser_user = { 0 };
	WCHAR sid[512] = { 0 };
	tokenuser.User = tokenuser_user;
	tokenuser_user.Sid = sid;

	sidStringBuffer = ExAllocatePool(NonPagedPool, 128);
	RtlInitEmptyUnicodeString(sidString, sidStringBuffer, 128);

	Token = PsReferencePrimaryToken(PsGetCurrentProcess());

	ntStatus = ObOpenObjectByPointer(
		Token,
		OBJ_KERNEL_HANDLE,
		AccessState,
		TOKEN_QUERY,
		NULL,
		KernelMode,
		&tokenHandle);

	ObDereferenceObject(Token);
	if (!NT_SUCCESS(ntStatus)) {
		//KdPrint(("\nGetSID: Could not open process token: %x\n", ntStatus ));
		return STATUS_FAIL_CHECK;
	}

	//
	// Pull out the SID
	//

	ntStatus = ZwQueryInformationToken(
		tokenHandle,
		TokenUser,
		NULL,
		0,
		&requiredLength);

	if (ntStatus != STATUS_BUFFER_TOO_SMALL) {

		KdPrint(("\nGetSID: Error getting token information: %x\n", ntStatus));
		ZwClose(tokenHandle);
		return STATUS_FAIL_CHECK;
	}

	tokenInfoBuffer = (PTOKEN_USER)ExAllocatePool(NonPagedPool, requiredLength);
	if (tokenInfoBuffer) {
		ntStatus = ZwQueryInformationToken(
			tokenHandle,
			TokenUser,
			tokenInfoBuffer,
			requiredLength,
			&requiredLength);
	}

	if (!NT_SUCCESS(ntStatus) || !tokenInfoBuffer) {
		KdPrint(("\nGetSID: Error getting token information: %x\n", ntStatus));
		if (tokenInfoBuffer) ExFreePool(tokenInfoBuffer);
		ZwClose(tokenHandle);
		return STATUS_FAIL_CHECK;
	}

	ZwClose(tokenHandle);


	//
	// Got it, now convert to text representation
	//
	ntStatus = RtlConvertSidToUnicodeString(sidString, tokenInfoBuffer->User.Sid, FALSE);
	sidString->Buffer[sidString->Length + 1] = '\0';
	KdPrint(("\nGetSID: sidString = %ws\n", sidString->Buffer));

	SIDLength = RtlLengthSid(tokenInfoBuffer->User.Sid);
	if (FALSE == RtlValidSid(tokenInfoBuffer->User.Sid)) return STATUS_FAIL_CHECK;

	//ntStatus = SecLookupAccountSid(
	//	tokenInfoBuffer-> User.Sid,
	//	&nameSize,	//PULONG          NameSize,
	//	&nameString, //PUNICODE_STRING NameBuffer,
	//	&domainSize, //PULONG          DomainSize,
	//	&domainString,//PUNICODE_STRING DomainBuffer,
	//	&nameUse 	//PSID_NAME_USE   NameUse
	//);

	//if (nameUse != SidTypeUser) return STATUS_FAIL_CHECK;

	//if(ntStatus == STATUS_BUFFER_TOO_SMALL){
	//	//
	//	KdPrint(("\nGetSID: Unable to convert SID to Name: %x\n", ntStatus ));
	//	nameBuffer = ExAllocatePool(NonPagedPool, nameSize*2);
	//	domainBuffer = ExAllocatePool(NonPagedPool, domainSize*2);
	//	RtlInitEmptyUnicodeString(&nameString, nameBuffer, nameSize*2);
	//	RtlInitEmptyUnicodeString(&domainString, domainBuffer, domainSize*2);

	//	ntStatus = SecLookupAccountSid(
	//		tokenInfoBuffer->User.Sid,
	//		&nameSize,	//PULONG          NameSize,
	//		&nameString, //PUNICODE_STRING NameBuffer,
	//		&domainSize, //PULONG          DomainSize,
	//		&domainString,//PUNICODE_STRING DomainBuffer,
	//		&nameUse 	//PSID_NAME_USE   NameUse
	//		);
	//	
	//	if (!NT_SUCCESS(ntStatus)){
	//		KdPrint(("\nGetSID: Unable to convert SID to Name: %x\n", ntStatus));
	//		ExFreePool(nameBuffer);
	//		ExFreePool(domainBuffer);
	//	}
	//}

	//if(NT_SUCCESS( ntStatus )){
	//	KdPrint(("\nGetSID: Unable to convert SID to Name: %x\n", ntStatus ));
	//	ExFreePool(nameBuffer);
	//	ExFreePool(domainBuffer);
	//}

	ExFreePool(tokenInfoBuffer);
	if (!NT_SUCCESS(ntStatus)) {
		KdPrint(("\nGetSID: Unable to convert SID to text: %x\n", ntStatus));
		return STATUS_FAIL_CHECK;
	}
	return STATUS_SUCCESS;
}