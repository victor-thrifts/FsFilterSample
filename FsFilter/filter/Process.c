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