/*++

Module Name:

    FsFilterSample.c

Abstract:

    This is the main module of the FsFilterSample miniFilter driver.

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>
#include <Ntstrsafe.h>

#include "FsFilterSample.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")


PFLT_FILTER gFilterHandle;
ULONG_PTR OperationStatusCtx = 1;

/*************************************************************************
Pool Tags
*************************************************************************/

#define BUFFER_SWAP_TAG     'bdBS'
#define CONTEXT_TAG         'xcBS'
#define NAME_TAG            'mnBS'
#define PRE_2_POST_TAG      'ppBS'
#define TEST_TAG			'test'


/*************************************************************************
Local structures
*************************************************************************/

//
//  This is a volume context, one of these are attached to each volume
//  we monitor.  This is used to get a "DOS" name for debug display.
//

const UNICODE_STRING ProtectedDirName = RTL_CONSTANT_STRING(L"\\EncryptionMinifilterDir\\");
const UNICODE_STRING ProtectedFilExt = RTL_CONSTANT_STRING(L".txt");

typedef struct _VOLUME_CONTEXT {

	//
	//  Holds the name to display
	//

	UNICODE_STRING Name;

	//
	//  Holds the sector size for this volume.
	//

	ULONG SectorSize;

} VOLUME_CONTEXT, *PVOLUME_CONTEXT;

//
//  This is a context structure that is used to pass state from our
//  pre-operation callback to our post-operation callback.
//
typedef struct _PRE_2_POST_CONTEXT {
	//
	//  Pointer to our volume context structure.  We always get the context
	//  in the preOperation path because you can not safely get it at DPC
	//  level.  We then release it in the postOperation path.  It is safe
	//  to release contexts at DPC level.
	//
	PVOLUME_CONTEXT VolCtx;

	//PSTREAM_CONTEXT pStreamCtx;

	//
	//  Since the post-operation parameters always receive the "original"
	//  parameters passed to the operation, we need to pass our new destination
	//  buffer to our post operation routine so we can free it.
	//
	PVOID SwappedBuffer;

} PRE_2_POST_CONTEXT, *PPRE_2_POST_CONTEXT;
//
//  This is a lookAside list used to allocate our pre-2-post structure.
//
NPAGED_LOOKASIDE_LIST Pre2PostContextList;

/*************************************************************************
    Prototypes
*************************************************************************/

EXTERN_C_START

BOOLEAN IsProtectedDir(PFLT_CALLBACK_DATA Data);

DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    );

NTSTATUS
FsFilterSampleInstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    );

VOID
FsFilterSampleInstanceTeardownStart (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    );

VOID
FsFilterSampleInstanceTeardownComplete (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    );

NTSTATUS
FsFilterSampleUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    );

NTSTATUS
FsFilterSampleInstanceQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
FsFilterSamplePreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

VOID
FsFilterSampleOperationStatusCallback (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PFLT_IO_PARAMETER_BLOCK ParameterSnapshot,
    _In_ NTSTATUS OperationStatus,
    _In_ PVOID RequesterContext
    );

FLT_POSTOP_CALLBACK_STATUS
FsFilterSamplePostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
FsFilterSamplePreOperationNoPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

BOOLEAN
FsFilterSampleDoRequestOperationStatus(
    _In_ PFLT_CALLBACK_DATA Data
    );

FLT_PREOP_CALLBACK_STATUS
PreReadBuffers(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
);

FLT_PREOP_CALLBACK_STATUS
PreDirCtrlBuffers(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
);

FLT_PREOP_CALLBACK_STATUS
PreWriteBuffers(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
);

VOID
CleanupVolumeContext(
	_In_ PFLT_CONTEXT Context,
	_In_ FLT_CONTEXT_TYPE ContextType
);

VOID
ReadDriverParameters(
	_In_ PUNICODE_STRING RegistryPath
);

FLT_POSTOP_CALLBACK_STATUS
PostReadBuffers(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_opt_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
);

FLT_POSTOP_CALLBACK_STATUS
SwapPostReadBuffersWhenSafe(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
);

FLT_POSTOP_CALLBACK_STATUS
PostWriteBuffers(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
);

FLT_POSTOP_CALLBACK_STATUS
PostDirCtrlBuffers(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
);

FLT_POSTOP_CALLBACK_STATUS
SwapPostDirCtrlBuffersWhenSafe(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__in PVOID CompletionContext,
	__in FLT_POST_OPERATION_FLAGS Flags
);

EXTERN_C_END

//
//  Assign text sections for each routine.
//

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(INIT, ReadDriverParameters)
#pragma alloc_text(PAGE, FsFilterSampleUnload)
#pragma alloc_text(PAGE, CleanupVolumeContext)
#pragma alloc_text(PAGE, FsFilterSampleInstanceQueryTeardown)
#pragma alloc_text(PAGE, FsFilterSampleInstanceSetup)
#pragma alloc_text(PAGE, FsFilterSampleInstanceTeardownStart)
#pragma alloc_text(PAGE, FsFilterSampleInstanceTeardownComplete)
#endif


VOID
ReadDriverParameters(
	_In_ PUNICODE_STRING RegistryPath
)
/*++

Routine Description:

This routine tries to read the driver-specific parameters from
the registry.  These values will be found in the registry location
indicated by the RegistryPath passed in.

Arguments:

RegistryPath - the path key passed to the driver during driver entry.

Return Value:

None.

--*/
{
	OBJECT_ATTRIBUTES attributes;
	HANDLE driverRegKey;
	NTSTATUS status;
	ULONG resultLength;
	UNICODE_STRING valueName;
	UCHAR buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(LONG)];

	//
	//  If this value is not zero then somebody has already explicitly set it
	//  so don't override those settings.
	//

	if (0 == LoggingFlags) {

		//
		//  Open the desired registry key
		//

		InitializeObjectAttributes(&attributes,
			RegistryPath,
			OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
			NULL,
			NULL);

		status = ZwOpenKey(&driverRegKey,
			KEY_READ,
			&attributes);

		if (!NT_SUCCESS(status)) {

			return;
		}

		//
		// Read the given value from the registry.
		//

		RtlInitUnicodeString(&valueName, L"DebugFlags");

		status = ZwQueryValueKey(driverRegKey,
			&valueName,
			KeyValuePartialInformation,
			buffer,
			sizeof(buffer),
			&resultLength);

		if (NT_SUCCESS(status)) {

			LoggingFlags = *((PULONG) &(((PKEY_VALUE_PARTIAL_INFORMATION)buffer)->Data));
		}

		//
		//  Close the registry entry
		//

		ZwClose(driverRegKey);
	}
}


BOOLEAN IsProtectedDir(PFLT_CALLBACK_DATA Data)
{
	PFLT_FILE_NAME_INFORMATION FileNameInformation = NULL;

	NTSTATUS status;

	status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED, &FileNameInformation);

	if (NT_SUCCESS(status))
	{
		status = FltParseFileNameInformation(FileNameInformation);

		if (NT_SUCCESS(status))
		{
			if ((NULL == FileNameInformation->ParentDir.Buffer) || (0 == FileNameInformation->ParentDir.Length)) 
			{
				return FALSE;
			}

			//KdPrint(("Parent Dir is %S\n", FileNameInformation->ParentDir.Buffer));
			if (RtlCompareUnicodeString(&FileNameInformation->ParentDir, &ProtectedDirName, FALSE) == 0)
			{
				//DbgPrint("\n Compare");
				FltReleaseFileNameInformation(FileNameInformation);
				return TRUE;
			}
			if (RtlCompareMemory(FileNameInformation->ParentDir.Buffer, ProtectedDirName.Buffer, sizeof(WCHAR) * 11) == 0)
			{
				KdPrint((" !!!!! sTARt with test \n"));
				FltReleaseFileNameInformation(FileNameInformation);
				return TRUE;
			}

			FltReleaseFileNameInformation(FileNameInformation);
		}
		else
		{
			KdPrint(("swapbuffers!IsProtectedDir : Error FltParseFileNameInformation "));
		}
	}
	else if (status == STATUS_FLT_INVALID_NAME_REQUEST)
	{
		KdPrint(("swapbuffers!IsProtectedDir : Error STATUS_FLT_INVALID_NAME_REQUEST return by FltGetFileNameInformation "));
	}
	else if (status == STATUS_INSUFFICIENT_RESOURCES)
	{
		KdPrint(("swapbuffers!IsProtectedDir : Error STATUS_INSUFFICIENT_RESOURCES return by FltGetFileNameInformation "));
	}
	else if (status == STATUS_INVALID_PARAMETER)
	{
		KdPrint(("swapbuffers!IsProtectedDir : Error STATUS_INVALID_PARAMETER return by FltGetFileNameInformation "));
	}
	return FALSE;
}

//
//  operation registration
//

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {

	{ IRP_MJ_READ,
	0,
	FsFilterSamplePreOperation,
	FsFilterSamplePostOperation },

	{ IRP_MJ_WRITE,
	0,
	FsFilterSamplePreOperation,
	FsFilterSamplePostOperation },

	{ IRP_MJ_DIRECTORY_CONTROL,
	0,
	FsFilterSamplePreOperation,
	FsFilterSamplePostOperation },

#if 0 // TODO - List all of the requests to filter.

	{ IRP_MJ_CREATE,
	0,
	FsFilterSamplePreOperation,
	FsFilterSamplePostOperation },

    { IRP_MJ_CREATE_NAMED_PIPE,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_CLOSE,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_QUERY_INFORMATION,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_SET_INFORMATION,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_QUERY_EA,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_SET_EA,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_FLUSH_BUFFERS,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_QUERY_VOLUME_INFORMATION,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_SET_VOLUME_INFORMATION,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_FILE_SYSTEM_CONTROL,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_DEVICE_CONTROL,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_INTERNAL_DEVICE_CONTROL,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_SHUTDOWN,
      0,
      FsFilterSamplePreOperationNoPostOperation,
      NULL },                               //post operations not supported

    { IRP_MJ_LOCK_CONTROL,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_CLEANUP,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_CREATE_MAILSLOT,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_QUERY_SECURITY,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_SET_SECURITY,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_QUERY_QUOTA,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_SET_QUOTA,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_PNP,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_ACQUIRE_FOR_MOD_WRITE,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_RELEASE_FOR_MOD_WRITE,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_ACQUIRE_FOR_CC_FLUSH,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_RELEASE_FOR_CC_FLUSH,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_NETWORK_QUERY_OPEN,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_MDL_READ,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_MDL_READ_COMPLETE,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_PREPARE_MDL_WRITE,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_MDL_WRITE_COMPLETE,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_VOLUME_MOUNT,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

    { IRP_MJ_VOLUME_DISMOUNT,
      0,
      FsFilterSamplePreOperation,
      FsFilterSamplePostOperation },

#endif // TODO

    { IRP_MJ_OPERATION_END }
};

//
//  Context definitions we currently care about.  Note that the system will
//  create a lookAside list for the volume context because an explicit size
//  of the context is specified.
//

CONST FLT_CONTEXT_REGISTRATION ContextNotifications[] = {

	{ FLT_VOLUME_CONTEXT,
	0,
	CleanupVolumeContext,
	sizeof(VOLUME_CONTEXT),
	CONTEXT_TAG },

{ FLT_CONTEXT_END }
};

//
//  This defines what we want to filter with FltMgr
//

CONST FLT_REGISTRATION FilterRegistration = {

    sizeof( FLT_REGISTRATION ),         //  Size
    FLT_REGISTRATION_VERSION,           //  Version
    0,                                  //  Flags

	ContextNotifications,               //  Context
    Callbacks,                          //  Operation callbacks

    FsFilterSampleUnload,                           //  MiniFilterUnload

    FsFilterSampleInstanceSetup,                    //  InstanceSetup
    FsFilterSampleInstanceQueryTeardown,            //  InstanceQueryTeardown
    FsFilterSampleInstanceTeardownStart,            //  InstanceTeardownStart
    FsFilterSampleInstanceTeardownComplete,         //  InstanceTeardownComplete

    NULL,                               //  GenerateFileName
    NULL,                               //  GenerateDestinationFileName
    NULL                                //  NormalizeNameComponent

};



NTSTATUS
FsFilterSampleInstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    )
/*++

Routine Description:

    This routine is called whenever a new instance is created on a volume. This
    gives us a chance to decide if we need to attach to this volume or not.

    If this routine is not defined in the registration structure, automatic
    instances are always created.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Flags describing the reason for this attach request.

Return Value:

    STATUS_SUCCESS - attach
    STATUS_FLT_DO_NOT_ATTACH - do not attach

--*/
{
	PDEVICE_OBJECT devObj = NULL;
	PVOLUME_CONTEXT ctx = NULL;
	NTSTATUS status = STATUS_FLT_DO_NOT_ATTACH;
	ULONG retLen;
	PUNICODE_STRING workingName;
	USHORT size;
	UCHAR volPropBuffer[sizeof(FLT_VOLUME_PROPERTIES) + 512];
	PFLT_VOLUME_PROPERTIES volProp = (PFLT_VOLUME_PROPERTIES)volPropBuffer;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(Flags);
	UNREFERENCED_PARAMETER(VolumeDeviceType);
	UNREFERENCED_PARAMETER(VolumeFilesystemType);

	try {

		//Allocate a volume context structure.
		status = FltAllocateContext(FltObjects->Filter, FLT_VOLUME_CONTEXT, sizeof(VOLUME_CONTEXT), NonPagedPool, &ctx);
		if (!NT_SUCCESS(status))
		{
			if (ctx)
			{
				FltDeleteContext(ctx);
			}

			leave;
		}

		//Always get the volume properties, so I can get a sector size
		status = FltGetVolumeProperties(FltObjects->Volume, volProp, sizeof(volPropBuffer), &retLen);
		if (!NT_SUCCESS(status))
		{
			if (ctx)
			{
				FltDeleteContext(ctx);
			}

			leave;
		}

		//
		//  Save the sector size in the context for later use.  Note that
		//  we will pick a minimum sector size if a sector size is not
		//  specified.
		//

		ASSERT((volProp->SectorSize == 0) || (volProp->SectorSize >= MIN_SECTOR_SIZE));

		ctx->SectorSize = max(volProp->SectorSize, MIN_SECTOR_SIZE);

		//
		//  Init the buffer field (which may be allocated later).
		//

		ctx->Name.Buffer = NULL;


		//Get the storage device object we want a name for.
		status = FltGetDiskDeviceObject(FltObjects->Volume, &devObj);
		if (NT_SUCCESS(status))
			status = RtlVolumeDeviceToDosName(devObj, &ctx->Name); //Try and get the DOS name.

																   //If we could not get a DOS name, get the NT name.
		if (!NT_SUCCESS(status)) {

			ASSERT(ctx->Name.Buffer == NULL);

			//Figure out which name to use from the properties
			if (volProp->RealDeviceName.Length > 0)
			{
				workingName = &volProp->RealDeviceName;

			}
			else if (volProp->FileSystemDeviceName.Length > 0) {

				workingName = &volProp->FileSystemDeviceName;

			}
			else {
				status = STATUS_FLT_DO_NOT_ATTACH;  //No name, don't save the context
				leave;
			}

			size = workingName->Length + sizeof(WCHAR); //length plus a trailing colon
			ctx->Name.Buffer = ExAllocatePoolWithTag(NonPagedPool, size, NAME_TAG);
			if (ctx->Name.Buffer == NULL) {

				status = STATUS_INSUFFICIENT_RESOURCES;
				leave;
			}
			ctx->Name.Length = 0;
			ctx->Name.MaximumLength = size;
			RtlCopyUnicodeString(&ctx->Name, workingName);
			RtlAppendUnicodeToString(&ctx->Name, L":");
#ifdef DBG
			DbgPrint("VolumeName:%wZ\n", &ctx->Name);
#endif
		}

		//Set the context
		status = FltSetVolumeContext(FltObjects->Volume, FLT_SET_CONTEXT_KEEP_IF_EXISTS, ctx, NULL);
		//  Log debug info
		LOG_PRINT(LOGFL_VOLCTX,
			("SwapBuffers!InstanceSetup:                  Real SectSize=0x%04x, Used SectSize=0x%04x, Name=\"%wZ\"\n",
				volProp->SectorSize,
				ctx->SectorSize,
				&ctx->Name));
		if (status == STATUS_FLT_CONTEXT_ALREADY_DEFINED) //It is OK for the context to already be defined.
			status = STATUS_SUCCESS;

	}
	finally{

		//
		//  Always release the context.  If the set failed, it will free the
		//  context.  If not, it will remove the reference added by the set.
		//  Note that the name buffer in the ctx will get freed by the context
		//  cleanup routine.
		//
#ifdef DBG
		DbgPrint("VolumeName:%wZ\n", &ctx->Name);
#endif

	if (ctx)
	{
#if DBG			
		if ((ctx->Name.Buffer[0] == L'C') || (ctx->Name.Buffer[0] == L'c'))
		{

			if (ctx->Name.Buffer != NULL)
			{
				ExFreePool(ctx->Name.Buffer);
				ctx->Name.Buffer = NULL;
			}

			status = STATUS_FLT_DO_NOT_ATTACH;
		}
#endif
		FltReleaseContext(ctx); // system will hang if not call this routine

		ctx = NULL;
	}

	if (devObj)
		ObDereferenceObject(devObj);//Remove the reference added by FltGetDiskDeviceObject.
	}

	return status;
}


NTSTATUS
FsFilterSampleInstanceQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This is called when an instance is being manually deleted by a
    call to FltDetachVolume or FilterDetach thereby giving us a
    chance to fail that detach request.

    If this routine is not defined in the registration structure, explicit
    detach requests via FltDetachVolume or FilterDetach will always be
    failed.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Indicating where this detach request came from.

Return Value:

    Returns the status of this operation.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterSample!FsFilterSampleInstanceQueryTeardown: Entered\n") );

    return STATUS_SUCCESS;
}


VOID
FsFilterSampleInstanceTeardownStart (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This routine is called at the start of instance teardown.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Reason why this instance is being deleted.

Return Value:

    None.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterSample!FsFilterSampleInstanceTeardownStart: Entered\n") );
}


VOID
FsFilterSampleInstanceTeardownComplete (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This routine is called at the end of instance teardown.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Reason why this instance is being deleted.

Return Value:

    None.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterSample!FsFilterSampleInstanceTeardownComplete: Entered\n") );
}


/*************************************************************************
    MiniFilter initialization and unload routines.
*************************************************************************/

NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:

    This is the initialization routine for this miniFilter driver.  This
    registers with FltMgr and initializes all global data structures.

Arguments:

    DriverObject - Pointer to driver object created by the system to
        represent this driver.

    RegistryPath - Unicode string identifying where the parameters for this
        driver are located in the registry.

Return Value:

    Routine can return non success error codes.

--*/
{
    NTSTATUS status;

	//
	//  Get debug trace flags
	//

	ReadDriverParameters(RegistryPath);

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterSample!DriverEntry: Entered\n") );

	//
	//  Init lookaside list used to allocate our context structure used to
	//  pass information from out preOperation callback to our postOperation
	//  callback.
	//
	ExInitializeNPagedLookasideList(&Pre2PostContextList, NULL, NULL, 0, sizeof(PRE_2_POST_CONTEXT), PRE_2_POST_TAG, 0);
	DbgPrint("Compile Date:%s\nCompile Time:%s\nEnter DriverEntry!\n", __DATE__, __TIME__);

#if DBGS
	_asm int 3
#endif

    //
    //  Register with FltMgr to tell it our callback routines
    //

    status = FltRegisterFilter( DriverObject,
                                &FilterRegistration,
                                &gFilterHandle );

    FLT_ASSERT( NT_SUCCESS( status ) );

    if (NT_SUCCESS( status )) {

        //
        //  Start filtering i/o
        //

        status = FltStartFiltering( gFilterHandle );

        if (!NT_SUCCESS( status )) {

            FltUnregisterFilter( gFilterHandle );
        }
    }

    return status;
}

NTSTATUS
FsFilterSampleUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    )
/*++

Routine Description:

    This is the unload routine for this miniFilter driver. This is called
    when the minifilter is about to be unloaded. We can fail this unload
    request if this is not a mandatory unload indicated by the Flags
    parameter.

Arguments:

    Flags - Indicating if this is a mandatory unload.

Return Value:

    Returns STATUS_SUCCESS.

--*/
{
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterSample!FsFilterSampleUnload: Entered\n") );

	//  Unregister from FLT mgr
	FltUnregisterFilter(gFilterHandle);

	//  Delete lookaside list
	ExDeleteNPagedLookasideList(&Pre2PostContextList);

	return STATUS_SUCCESS;
}

/*************************************************************************
    MiniFilter callback routines.
*************************************************************************/
FLT_PREOP_CALLBACK_STATUS
FsFilterSamplePreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )
/*++

Routine Description:

    This routine is a pre-operation dispatch routine for this miniFilter.

    This is non-pageable because it could be called on the paging path

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The context for the completion routine for this
        operation.

Return Value:

    The return value is the status of the operation.

--*/
{
    NTSTATUS status;
	PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;
	FLT_PREOP_CALLBACK_STATUS retValue = FLT_PREOP_SUCCESS_NO_CALLBACK;	  //FLT_PREOP_SUCCESS_WITH_CALLBACK

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,	("FsFilterSample!FsFilterSamplePreOperation: Entered\n") );
	DbgPrint("\n MN=0x%08x IRP=0x%08x", iopb->MajorFunction, iopb->MinorFunction, iopb->IrpFlags);

	if (IRP_MJ_READ == iopb->MajorFunction) {
		retValue = PreReadBuffers(Data, FltObjects, CompletionContext);
	}
	else if(IRP_MJ_WRITE == iopb->MajorFunction) {
		retValue = PreWriteBuffers(Data, FltObjects, CompletionContext);
	}
	else if(IRP_MJ_DIRECTORY_CONTROL == iopb->MajorFunction) {
		retValue = PreDirCtrlBuffers(Data, FltObjects, CompletionContext);
	}
	

    //
    //  See if this is an operation we would like the operation status
    //  for.  If so request it.
    //
    //  NOTE: most filters do NOT need to do this.  You only need to make
    //        this call if, for example, you need to know if the oplock was
    //        actually granted.
    //

    if (FsFilterSampleDoRequestOperationStatus( Data )) {

        status = FltRequestOperationStatusCallback( Data,
                                                    FsFilterSampleOperationStatusCallback,
                                                    (PVOID)(++OperationStatusCtx) );
        if (!NT_SUCCESS(status)) {

            PT_DBG_PRINT( PTDBG_TRACE_OPERATION_STATUS,
                          ("FsFilterSample!FsFilterSamplePreOperation: FltRequestOperationStatusCallback Failed, status=%08x\n",
                           status) );
        }
		retValue = FLT_PREOP_SUCCESS_WITH_CALLBACK;
    }

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_WITH_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return retValue;
}



VOID
FsFilterSampleOperationStatusCallback (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PFLT_IO_PARAMETER_BLOCK ParameterSnapshot,
    _In_ NTSTATUS OperationStatus,
    _In_ PVOID RequesterContext
    )
/*++

Routine Description:

    This routine is called when the given operation returns from the call
    to IoCallDriver.  This is useful for operations where STATUS_PENDING
    means the operation was successfully queued.  This is useful for OpLocks
    and directory change notification operations.

    This callback is called in the context of the originating thread and will
    never be called at DPC level.  The file object has been correctly
    referenced so that you can access it.  It will be automatically
    dereferenced upon return.

    This is non-pageable because it could be called on the paging path

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    RequesterContext - The context for the completion routine for this
        operation.

    OperationStatus -

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterSample!FsFilterSampleOperationStatusCallback: Entered\n") );

    PT_DBG_PRINT( PTDBG_TRACE_OPERATION_STATUS,
                  ("FsFilterSample!FsFilterSampleOperationStatusCallback: Status=%08x ctx=%p IrpMj=%02x.%02x \"%s\"\n",
                   OperationStatus,
                   RequesterContext,
                   ParameterSnapshot->MajorFunction,
                   ParameterSnapshot->MinorFunction,
                   FltGetIrpName(ParameterSnapshot->MajorFunction)) );
}


FLT_POSTOP_CALLBACK_STATUS
FsFilterSamplePostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    )
/*++

Routine Description:

    This routine is the post-operation completion routine for this
    miniFilter.

    This is non-pageable because it may be called at DPC level.

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The completion context set in the pre-operation routine.

    Flags - Denotes whether the completion is successful or is being drained.

Return Value:

    The return value is the status of the operation.

--*/
{
	PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;

	FLT_POSTOP_CALLBACK_STATUS retValue = FLT_POSTOP_FINISHED_PROCESSING;

	PT_DBG_PRINT(PTDBG_TRACE_ROUTINES,
		("FsFilterSample!FsFilterSamplePostOperation: Entered\n"));

	DbgPrint("\n PostRead 0x%08x : 0x%08x", iopb->MinorFunction, iopb->IrpFlags);

	if (IRP_MJ_READ == iopb->MajorFunction) {
		retValue = PostReadBuffers(Data, FltObjects, CompletionContext, Flags);
	} else if (IRP_MJ_WRITE == iopb->MajorFunction) {
		retValue = PostWriteBuffers(Data, FltObjects, CompletionContext, Flags);
	} else if (IRP_MJ_DIRECTORY_CONTROL == iopb->MajorFunction) {
		retValue = PostDirCtrlBuffers(Data, FltObjects, CompletionContext, Flags);
	}

	return retValue;
}


FLT_PREOP_CALLBACK_STATUS
FsFilterSamplePreOperationNoPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )
/*++

Routine Description:

    This routine is a pre-operation dispatch routine for this miniFilter.

    This is non-pageable because it could be called on the paging path

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The context for the completion routine for this
        operation.

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( Data );
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterSample!FsFilterSamplePreOperationNoPostOperation: Entered\n") );

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_NO_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}


BOOLEAN
FsFilterSampleDoRequestOperationStatus(
    _In_ PFLT_CALLBACK_DATA Data
    )
/*++

Routine Description:

    This identifies those operations we want the operation status for.  These
    are typically operations that return STATUS_PENDING as a normal completion
    status.

Arguments:

Return Value:

    TRUE - If we want the operation status
    FALSE - If we don't

--*/
{
    PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;

    //
    //  return boolean state based on which operations we are interested in
    //

    return (BOOLEAN)

            //
            //  Check for oplock operations
            //

             (((iopb->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL) &&
               ((iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_FILTER_OPLOCK)  ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_BATCH_OPLOCK)   ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_OPLOCK_LEVEL_1) ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_OPLOCK_LEVEL_2)))

              ||

              //
              //    Check for directy change notification
              //

              ((iopb->MajorFunction == IRP_MJ_DIRECTORY_CONTROL) &&
               (iopb->MinorFunction == IRP_MN_NOTIFY_CHANGE_DIRECTORY))
             );
}


BOOLEAN IsProtectionFile(PFLT_FILE_NAME_INFORMATION NameInfos)
{
	BOOLEAN bProtect = FALSE;

	UNICODE_STRING 	ProtectionFileName, FileName;

	// 复制数据
	RtlInitUnicodeString(&ProtectionFileName, ProtectedFilExt.Buffer);

	RtlInitUnicodeString(&FileName, NameInfos->Name.Buffer);

	// 判断
	if (NULL != wcsstr(FileName.Buffer, ProtectionFileName.Buffer))
	{
		bProtect = TRUE;
	}

	// 释放内存
	RtlFreeUnicodeString(&ProtectionFileName);

	RtlFreeUnicodeString(&FileName);

	return bProtect;
}


FLT_PREOP_CALLBACK_STATUS
PreReadBuffers(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
)
/*++

Routine Description:

This routine demonstrates how to swap buffers for the READ operation.

Note that it handles all errors by simply not doing the buffer swap.

Arguments:

Data - Pointer to the filter callbackData that is passed to us.

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance, its associated volume and
file object.

CompletionContext - Receives the context that will be passed to the
post-operation callback.

Return Value:

FLT_PREOP_SUCCESS_WITH_CALLBACK - we want a postOpeation callback
FLT_PREOP_SUCCESS_NO_CALLBACK - we don't want a postOperation callback

--*/
{
	PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;
	FLT_PREOP_CALLBACK_STATUS retValue = FLT_PREOP_SUCCESS_NO_CALLBACK;
	PVOID newBuf = NULL;
	PMDL newMdl = NULL;
	PVOLUME_CONTEXT volCtx = NULL;
	PPRE_2_POST_CONTEXT p2pCtx;
	NTSTATUS status;
	ULONG readLen = iopb->Parameters.Read.Length;

	try {

		//
		//  If they are trying to read ZERO bytes, then don't do anything and
		//  we don't need a post-operation callback.
		//

		if (IsProtectedDir(Data) == FALSE)
		{
			leave;
		}

		PFLT_FILE_NAME_INFORMATION	FileNameInformation = NULL;
		status = FltGetFileNameInformation(Data,
			FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
			&FileNameInformation);
		if (NT_SUCCESS(status)) {
			status = FltParseFileNameInformation(FileNameInformation);
			if (NT_SUCCESS(status)) {
				if (FALSE == IsProtectionFile(FileNameInformation))
				{
					leave;
				}
			}

		}


		DbgPrint("\n MN=0x%08x IRP=0x%08x RL=%d", iopb->MinorFunction, iopb->IrpFlags, readLen, retValue);

		if (readLen == 0) {

			leave;
		}

		if (FLT_IS_FASTIO_OPERATION(Data))
		{
			DbgPrint("\n FAST IO OPERATION");

			return FLT_PREOP_DISALLOW_FASTIO;
		}

		if (iopb->IrpFlags & IRP_PAGING_IO)
		{
			DbgPrint("\n IRP_PAGING_IO ");
		}
		else
		{
			DbgPrint("\n NOT IRP_PAGING_IO");

			return FLT_PREOP_SUCCESS_NO_CALLBACK;
		}

		DbgPrint("\n PreRead 0x%08x : 0x%08x", iopb->MinorFunction, iopb->IrpFlags);

		//
		//  Get our volume context so we can display our volume name in the
		//  debug output.
		//

		status = FltGetVolumeContext(FltObjects->Filter,
			FltObjects->Volume,
			&volCtx);

		if (!NT_SUCCESS(status)) {

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPreReadBuffers:             Error getting volume context, status=%x\n",
					status));

			leave;
		}

		//
		//  If this is a non-cached I/O we need to round the length up to the
		//  sector size for this device.  We must do this because the file
		//  systems do this and we need to make sure our buffer is as big
		//  as they are expecting.
		//

		if (iopb->IrpFlags & IRP_PAGING_IO) {

			DbgPrint("\n PreRead IRP : 0x%08x", iopb->IrpFlags);

		}

		if (FlagOn(IRP_NOCACHE, iopb->IrpFlags)) {

			readLen = (ULONG)ROUND_TO_SIZE(readLen, volCtx->SectorSize);

			DbgPrint("\n PreRead IRP_NOCACHE");
		}

		//
		//  Allocate nonPaged memory for the buffer we are swapping to.
		//  If we fail to get the memory, just don't swap buffers on this
		//  operation.
		//

		newBuf = ExAllocatePoolWithTag(NonPagedPool,
			readLen,
			BUFFER_SWAP_TAG);

		UNICODE_STRING sbcxt;
		RtlInitUnicodeString(&sbcxt, L"xxxxxxxxxxxxxxx");
		RtlCopyUnicodeString(newBuf, &sbcxt);

		if (newBuf == NULL) {

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPreReadBuffers:             %wZ Failed to allocate %d bytes of memory\n",
					&volCtx->Name,
					readLen));

			leave;
		}

		//
		//  We only need to build a MDL for IRP operations.  We don't need to
		//  do this for a FASTIO operation since the FASTIO interface has no
		//  parameter for passing the MDL to the file system.
		//

		if (FlagOn(Data->Flags, FLTFL_CALLBACK_DATA_IRP_OPERATION)) {

			//
			//  Allocate a MDL for the new allocated memory.  If we fail
			//  the MDL allocation then we won't swap buffer for this operation
			//

			newMdl = IoAllocateMdl(newBuf,
				readLen,
				FALSE,
				FALSE,
				NULL);

			if (newMdl == NULL) {

				LOG_PRINT(LOGFL_ERRORS,
					("SwapBuffers!SwapPreReadBuffers:             %wZ Failed to allocate MDL\n",
						&volCtx->Name));

				leave;
			}

			//
			//  setup the MDL for the non-paged pool we just allocated
			//

			MmBuildMdlForNonPagedPool(newMdl);
		}

		//
		//  We are ready to swap buffers, get a pre2Post context structure.
		//  We need it to pass the volume context and the allocate memory
		//  buffer to the post operation callback.
		//

		p2pCtx = ExAllocateFromNPagedLookasideList(&Pre2PostContextList);

		if (p2pCtx == NULL) {

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPreReadBuffers:             %wZ Failed to allocate pre2Post context structure\n",
					&volCtx->Name));

			leave;
		}

		//
		//  Log that we are swapping
		//

		LOG_PRINT(LOGFL_READ,
			("SwapBuffers!SwapPreReadBuffers:             %wZ newB=%p newMdl=%p oldB=%p oldMdl=%p len=%d\n",
				&volCtx->Name,
				newBuf,
				newMdl,
				iopb->Parameters.Read.ReadBuffer,
				iopb->Parameters.Read.MdlAddress,
				readLen));

		//
		//  Update the buffer pointers and MDL address, mark we have changed
		//  something.
		//	

		iopb->Parameters.Read.ReadBuffer = newBuf;
		iopb->Parameters.Read.MdlAddress = newMdl;
		FltSetCallbackDataDirty(Data);

		//
		//  Pass state to our post-operation callback.
		//

		p2pCtx->SwappedBuffer = newBuf;
		p2pCtx->VolCtx = volCtx;

		*CompletionContext = p2pCtx;

		//
		//  Return we want a post-operation callback
		//

		retValue = FLT_PREOP_SUCCESS_WITH_CALLBACK;

	}
	finally{

		//
		//  If we don't want a post-operation callback, then cleanup state.
		//

		if (retValue != FLT_PREOP_SUCCESS_WITH_CALLBACK) {

			if (newBuf != NULL) {

				ExFreePool(newBuf);
			}

			if (newMdl != NULL) {

				IoFreeMdl(newMdl);
			}

			if (volCtx != NULL) {

				FltReleaseContext(volCtx);
			}
		}
	}

		//DbgPrint("\n status=%x",retValue);

	return retValue;
}

FLT_PREOP_CALLBACK_STATUS
PreDirCtrlBuffers(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
)
/*++

Routine Description:

This routine demonstrates how to swap buffers for the Directory Control
operations.  The reason this routine is here is because directory change
notifications are long lived and this allows you to see how FltMgr
handles long lived IRP operations that have swapped buffers when the
mini-filter is unloaded.  It does this by canceling the IRP.

Note that it handles all errors by simply not doing the
buffer swap.

Arguments:

Data - Pointer to the filter callbackData that is passed to us.

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance, its associated volume and
file object.

CompletionContext - Receives the context that will be passed to the
post-operation callback.

Return Value:

FLT_PREOP_SUCCESS_WITH_CALLBACK - we want a postOpeation callback
FLT_PREOP_SUCCESS_NO_CALLBACK - we don't want a postOperation callback

--*/
{
	PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;
	FLT_PREOP_CALLBACK_STATUS retValue = FLT_PREOP_SUCCESS_NO_CALLBACK;
	PVOID newBuf = NULL;
	PMDL newMdl = NULL;
	PVOLUME_CONTEXT volCtx = NULL;
	PPRE_2_POST_CONTEXT p2pCtx;
	NTSTATUS status;

	try {

		//
		//  If they are trying to get ZERO bytes, then don't do anything and
		//  we don't need a post-operation callback.
		//

		if (iopb->Parameters.DirectoryControl.QueryDirectory.Length == 0) {

			leave;
		}

		//DbgPrint("\n PreDir 0x%08x : 0x%08x",iopb->MinorFunction,iopb->IrpFlags);

		//
		//  Get our volume context.  If we can't get it, just return.
		//

		status = FltGetVolumeContext(FltObjects->Filter,
			FltObjects->Volume,
			&volCtx);

		if (!NT_SUCCESS(status)) {

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPreDirCtrlBuffers:          Error getting volume context, status=%x\n",
					status));

			leave;
		}

		//
		//  Allocate nonPaged memory for the buffer we are swapping to.
		//  If we fail to get the memory, just don't swap buffers on this
		//  operation.
		//

		newBuf = ExAllocatePoolWithTag(NonPagedPool,
			iopb->Parameters.DirectoryControl.QueryDirectory.Length,
			BUFFER_SWAP_TAG);

		if (newBuf == NULL) {

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPreDirCtrlBuffers:          %wZ Failed to allocate %d bytes of memory.\n",
					&volCtx->Name,
					iopb->Parameters.DirectoryControl.QueryDirectory.Length));

			leave;
		}

		//
		//  We only need to build a MDL for IRP operations.  We don't need to
		//  do this for a FASTIO operation because it is a waste of time since
		//  the FASTIO interface has no parameter for passing the MDL to the
		//  file system.
		//

		if (FlagOn(Data->Flags, FLTFL_CALLBACK_DATA_IRP_OPERATION)) {

			//
			//  Allocate a MDL for the new allocated memory.  If we fail
			//  the MDL allocation then we won't swap buffer for this operation
			//

			newMdl = IoAllocateMdl(newBuf,
				iopb->Parameters.DirectoryControl.QueryDirectory.Length,
				FALSE,
				FALSE,
				NULL);

			if (newMdl == NULL) {

				LOG_PRINT(LOGFL_ERRORS,
					("SwapBuffers!SwapPreDirCtrlBuffers:          %wZ Failed to allocate MDL.\n",
						&volCtx->Name));

				leave;
			}

			//
			//  setup the MDL for the non-paged pool we just allocated
			//

			MmBuildMdlForNonPagedPool(newMdl);
		}

		//
		//  We are ready to swap buffers, get a pre2Post context structure.
		//  We need it to pass the volume context and the allocate memory
		//  buffer to the post operation callback.
		//

		p2pCtx = ExAllocateFromNPagedLookasideList(&Pre2PostContextList);

		if (p2pCtx == NULL) {

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPreDirCtrlBuffers:          %wZ Failed to allocate pre2Post context structure\n",
					&volCtx->Name));

			leave;
		}

		//
		//  Log that we are swapping
		//

		LOG_PRINT(LOGFL_DIRCTRL,
			("SwapBuffers!SwapPreDirCtrlBuffers:          %wZ newB=%p newMdl=%p oldB=%p oldMdl=%p len=%d\n",
				&volCtx->Name,
				newBuf,
				newMdl,
				iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer,
				iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress,
				iopb->Parameters.DirectoryControl.QueryDirectory.Length));

		//
		//  Update the buffer pointers and MDL address
		//

		iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer = newBuf;
		iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress = newMdl;
		FltSetCallbackDataDirty(Data);

		//
		//  Pass state to our post-operation callback.
		//

		p2pCtx->SwappedBuffer = newBuf;
		p2pCtx->VolCtx = volCtx;

		*CompletionContext = p2pCtx;

		//
		//  Return we want a post-operation callback
		//

		retValue = FLT_PREOP_SUCCESS_WITH_CALLBACK;

	}
	finally{

		//
		//  If we don't want a post-operation callback, then cleanup state.
		//

		if (retValue != FLT_PREOP_SUCCESS_WITH_CALLBACK) {

			if (newBuf != NULL) {

				ExFreePool(newBuf);
			}

			if (newMdl != NULL) {

				IoFreeMdl(newMdl);
			}

			if (volCtx != NULL) {

				FltReleaseContext(volCtx);
			}
		}
	}

	return retValue;
}

FLT_PREOP_CALLBACK_STATUS
PreWriteBuffers(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
)
/*++

Routine Description:

This routine demonstrates how to swap buffers for the WRITE operation.

Note that it handles all errors by simply not doing the buffer swap.

Arguments:

Data - Pointer to the filter callbackData that is passed to us.

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance, its associated volume and
file object.

CompletionContext - Receives the context that will be passed to the
post-operation callback.

Return Value:

FLT_PREOP_SUCCESS_WITH_CALLBACK - we want a postOpeation callback
FLT_PREOP_SUCCESS_NO_CALLBACK - we don't want a postOperation callback
FLT_PREOP_COMPLETE -
--*/
{
	PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;
	FLT_PREOP_CALLBACK_STATUS retValue = FLT_PREOP_SUCCESS_NO_CALLBACK;
	PVOID newBuf = NULL;
	PMDL newMdl = NULL;
	PVOLUME_CONTEXT volCtx = NULL;
	PPRE_2_POST_CONTEXT p2pCtx;
	PVOID origBuf;
	NTSTATUS status;
	ULONG writeLen = iopb->Parameters.Write.Length;

	//ULONG realWriteLen = volCtx->SectorSize;
	//ULONG copyCounter  =0;
	try {

		//
		//  If they are trying to write ZERO bytes, then don't do anything and
		//  we don't need a post-operation callback.
		//

		if (IsProtectedDir(Data) == FALSE)
		{
			leave;
		}

		if (writeLen == 0) {

			leave;
		}

		PFLT_FILE_NAME_INFORMATION	FileNameInformation = NULL;
		status = FltGetFileNameInformation(Data,
			FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
			&FileNameInformation);
		if (NT_SUCCESS(status)) {
			status = FltParseFileNameInformation(FileNameInformation);
			if (NT_SUCCESS(status)) {
				if (FALSE == IsProtectionFile(FileNameInformation))
				{
					leave;
				}
			}

		}

		DbgPrint("\n PostWrite 0x%08x : 0x%08x", iopb->MinorFunction, iopb->IrpFlags);

		//
		//  Get our volume context so we can display our volume name in the
		//  debug output.
		//

		status = FltGetVolumeContext(FltObjects->Filter,
			FltObjects->Volume,
			&volCtx);

		if (!NT_SUCCESS(status)) {

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPreWriteBuffers:            Error getting volume context, status=%x\n",
					status));

			leave;
		}

		//
		//  If this is a non-cached I/O we need to round the length up to the
		//  sector size for this device.  We must do this because the file
		//  systems do this and we need to make sure our buffer is as big
		//  as they are expecting.
		//

		if (iopb->IrpFlags & IRP_PAGING_IO) {

			DbgPrint("\n PreWrite IRP : 0x%08x", iopb->IrpFlags);

		}




		if (FlagOn(IRP_NOCACHE, iopb->IrpFlags)) {

			writeLen = (ULONG)ROUND_TO_SIZE(writeLen, volCtx->SectorSize);
		}

		//if(realWriteLen<=writeLen)
		//realWriteLen = writeLen;
		//
		//  Allocate nonPaged memory for the buffer we are swapping to.
		//  If we fail to get the memory, just don't swap buffers on this
		//  operation.
		//

		newBuf = ExAllocatePoolWithTag(NonPagedPool,
			writeLen,
			/*next line added by RT.2011.02.09
			realWriteLen,*/
			BUFFER_SWAP_TAG);

		if (newBuf == NULL) {

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPreWriteBuffers:            %wZ Failed to allocate %d bytes of memory.\n",
					&volCtx->Name,
					writeLen));

			leave;
		}

		RtlStringCbCopyW(newBuf, writeLen/2, L"aaaa");

		//
		//  We only need to build a MDL for IRP operations.  We don't need to
		//  do this for a FASTIO operation because it is a waste of time since
		//  the FASTIO interface has no parameter for passing the MDL to the
		//  file system.
		//

		if (FlagOn(Data->Flags, FLTFL_CALLBACK_DATA_IRP_OPERATION)) {

			//
			//  Allocate a MDL for the new allocated memory.  If we fail
			//  the MDL allocation then we won't swap buffer for this operation
			//

			newMdl = IoAllocateMdl(newBuf,
				writeLen,
				FALSE,
				FALSE,
				NULL);

			if (newMdl == NULL) {

				LOG_PRINT(LOGFL_ERRORS,
					("SwapBuffers!SwapPreWriteBuffers:            %wZ Failed to allocate MDL.\n",
						&volCtx->Name));

				leave;
			}

			//
			//  setup the MDL for the non-paged pool we just allocated
			//

			MmBuildMdlForNonPagedPool(newMdl);
		}

		//
		//  If the users original buffer had a MDL, get a system address.
		//

		if (iopb->Parameters.Write.MdlAddress != NULL) {

			origBuf = MmGetSystemAddressForMdlSafe(iopb->Parameters.Write.MdlAddress,
				NormalPagePriority);

			if (origBuf == NULL) {

				LOG_PRINT(LOGFL_ERRORS,
					("SwapBuffers!SwapPreWriteBuffers:            %wZ Failed to get system address for MDL: %p\n",
						&volCtx->Name,
						iopb->Parameters.Write.MdlAddress));

				//
				//  If we could not get a system address for the users buffer,
				//  then we are going to fail this operation.
				//

				Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
				Data->IoStatus.Information = 0;
				retValue = FLT_PREOP_COMPLETE;
				leave;
			}

		}
		else {

			//
			//  There was no MDL defined, use the given buffer address.
			//

			origBuf = iopb->Parameters.Write.WriteBuffer;
		}

		//
		//  Copy the memory, we must do this inside the try/except because we
		//  may be using a users buffer address
		//

		try {


			RtlCopyMemory(newBuf,
				origBuf,
				writeLen);
			//if condition added by RT.2011.02.09

			/*
			if(realWriteLen<=writeLen)
			RtlCopyMemory( newBuf, origBuf,writeLen);
			else{
			RtlCopyMemory(newBuf, origBuf, writeLen);
			//memset(newBuf+writeLen, '0', realWriteLen);
			}
			*/


			//DbgPrint("\nSwapPreWriteBuffers [%d][%d]",writeLen,iopb->Parameters.Write.ByteOffset);

		} except(EXCEPTION_EXECUTE_HANDLER) {

			//
			//  The copy failed, return an error, failing the operation.
			//

			Data->IoStatus.Status = GetExceptionCode();
			Data->IoStatus.Information = 0;
			retValue = FLT_PREOP_COMPLETE;

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPreWriteBuffers:            %wZ Invalid user buffer, oldB=%p, status=%x\n",
					&volCtx->Name,
					origBuf,
					Data->IoStatus.Status));

			leave;
		}

		//
		//  We are ready to swap buffers, get a pre2Post context structure.
		//  We need it to pass the volume context and the allocate memory
		//  buffer to the post operation callback.
		//

		p2pCtx = ExAllocateFromNPagedLookasideList(&Pre2PostContextList);

		if (p2pCtx == NULL) {

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPreWriteBuffers:            %wZ Failed to allocate pre2Post context structure\n",
					&volCtx->Name));

			leave;
		}

		//
		//  Set new buffers
		//

		LOG_PRINT(LOGFL_WRITE,
			("SwapBuffers!SwapPreWriteBuffers:            %wZ newB=%p newMdl=%p oldB=%p oldMdl=%p len=%d\n",
				&volCtx->Name,
				newBuf,
				newMdl,
				iopb->Parameters.Write.WriteBuffer,
				iopb->Parameters.Write.MdlAddress,
				writeLen));

		//iopb->Parameters.Write.Length = realWriteLen;
		iopb->Parameters.Write.WriteBuffer = newBuf;
		iopb->Parameters.Write.MdlAddress = newMdl;
		FltSetCallbackDataDirty(Data);

		//
		//  Pass state to our post-operation callback.
		//

		p2pCtx->SwappedBuffer = newBuf;
		p2pCtx->VolCtx = volCtx;

		*CompletionContext = p2pCtx;

		//
		//  Return we want a post-operation callback
		//

		retValue = FLT_PREOP_SUCCESS_WITH_CALLBACK;

	}
	finally{

		//
		//  If we don't want a post-operation callback, then free the buffer
		//  or MDL if it was allocated.
		//

		if (retValue != FLT_PREOP_SUCCESS_WITH_CALLBACK) {

			if (newBuf != NULL) {

				ExFreePool(newBuf);
			}

			if (newMdl != NULL) {

				IoFreeMdl(newMdl);
			}

			if (volCtx != NULL) {

				FltReleaseContext(volCtx);
			}
		}
	}

	return retValue;
}

VOID
CleanupVolumeContext(
	_In_ PFLT_CONTEXT Context,
	_In_ FLT_CONTEXT_TYPE ContextType
)
/*++

Routine Description:

The given context is being freed.
Free the allocated name buffer if there one.

Arguments:

Context - The context being freed

ContextType - The type of context this is

Return Value:

None

--*/
{
	PVOLUME_CONTEXT ctx = Context;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(ContextType);

	ASSERT(ContextType == FLT_VOLUME_CONTEXT);

	if (ctx->Name.Buffer != NULL) {

		ExFreePool(ctx->Name.Buffer);
		ctx->Name.Buffer = NULL;
	}
}


FLT_POSTOP_CALLBACK_STATUS
PostReadBuffers(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_opt_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
)
/*++

Routine Description:

This routine does postRead buffer swap handling

Arguments:

Data - Pointer to the filter callbackData that is passed to us.

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance, its associated volume and
file object.

CompletionContext - The completion context set in the pre-operation routine.

Flags - Denotes whether the completion is successful or is being drained.

Return Value:

FLT_POSTOP_FINISHED_PROCESSING
FLT_POSTOP_MORE_PROCESSING_REQUIRED

--*/
{
	PVOID origBuf;
	PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;
	FLT_POSTOP_CALLBACK_STATUS retValue = FLT_POSTOP_FINISHED_PROCESSING;
	PPRE_2_POST_CONTEXT p2pCtx = CompletionContext;
	BOOLEAN cleanupAllocatedBuffer = TRUE;

	//
	//  This system won't draining an operation with swapped buffers, verify
	//  the draining flag is not set.
	//

	DbgPrint("\n PostRead 0x%08x : 0x%08x", iopb->MinorFunction, iopb->IrpFlags);

	ASSERT(!FlagOn(Flags, FLTFL_POST_OPERATION_DRAINING));

	try {

		//
		//  If the operation failed or the count is zero, there is no data to
		//  copy so just return now.
		//



		if (!NT_SUCCESS(Data->IoStatus.Status) ||
			(Data->IoStatus.Information == 0)) {

			LOG_PRINT(LOGFL_READ,
				("SwapBuffers!SwapPostReadBuffers:            %wZ newB=%p No data read, status=%x, info=%x\n",
					&p2pCtx->VolCtx->Name,
					p2pCtx->SwappedBuffer,
					Data->IoStatus.Status,
					Data->IoStatus.Information));

			leave;
		}

		//
		//  We need to copy the read data back into the users buffer.  Note
		//  that the parameters passed in are for the users original buffers
		//  not our swapped buffers.
		//

		//DbgPrint("\n SwapPostReadBuffers [%d][%d]",iopb->Parameters.Read.Length,iopb->Parameters.Read.ByteOffset);

		if (iopb->Parameters.Read.MdlAddress != NULL) {

			//
			//  There is a MDL defined for the original buffer, get a
			//  system address for it so we can copy the data back to it.
			//  We must do this because we don't know what thread context
			//  we are in.
			//

			origBuf = MmGetSystemAddressForMdlSafe(iopb->Parameters.Read.MdlAddress,
				NormalPagePriority);

			if (origBuf == NULL) {

				LOG_PRINT(LOGFL_ERRORS,
					("SwapBuffers!SwapPostReadBuffers:            %wZ Failed to get system address for MDL: %p\n",
						&p2pCtx->VolCtx->Name,
						iopb->Parameters.Read.MdlAddress));

				//
				//  If we failed to get a SYSTEM address, mark that the read
				//  failed and return.
				//

				Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
				Data->IoStatus.Information = 0;
				leave;
			}

		}
		else if (FlagOn(Data->Flags, FLTFL_CALLBACK_DATA_SYSTEM_BUFFER) ||
			FlagOn(Data->Flags, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION)) {

			//
			//  If this is a system buffer, just use the given address because
			//      it is valid in all thread contexts.
			//  If this is a FASTIO operation, we can just use the
			//      buffer (inside a try/except) since we know we are in
			//      the correct thread context (you can't pend FASTIO's).
			//

			origBuf = iopb->Parameters.Read.ReadBuffer;

		}
		else {

			//
			//  They don't have a MDL and this is not a system buffer
			//  or a fastio so this is probably some arbitrary user
			//  buffer.  We can not do the processing at DPC level so
			//  try and get to a safe IRQL so we can do the processing.
			//

			if (FltDoCompletionProcessingWhenSafe(Data,
				FltObjects,
				CompletionContext,
				Flags,
				SwapPostReadBuffersWhenSafe,
				&retValue)) {

				//
				//  This operation has been moved to a safe IRQL, the called
				//  routine will do (or has done) the freeing so don't do it
				//  in our routine.
				//

				cleanupAllocatedBuffer = FALSE;

			}
			else {

				//
				//  We are in a state where we can not get to a safe IRQL and
				//  we do not have a MDL.  There is nothing we can do to safely
				//  copy the data back to the users buffer, fail the operation
				//  and return.  This shouldn't ever happen because in those
				//  situations where it is not safe to post, we should have
				//  a MDL.
				//

				LOG_PRINT(LOGFL_ERRORS,
					("SwapBuffers!SwapPostReadBuffers:            %wZ Unable to post to a safe IRQL\n",
						&p2pCtx->VolCtx->Name));

				Data->IoStatus.Status = STATUS_UNSUCCESSFUL;
				Data->IoStatus.Information = 0;
			}

			leave;
		}

		//
		//  We either have a system buffer or this is a fastio operation
		//  so we are in the proper context.  Copy the data handling an
		//  exception.
		//

		try {

			RtlCopyMemory(origBuf,
				p2pCtx->SwappedBuffer,
				Data->IoStatus.Information);


		} except(EXCEPTION_EXECUTE_HANDLER) {

			//
			//  The copy failed, return an error, failing the operation.
			//

			Data->IoStatus.Status = GetExceptionCode();
			Data->IoStatus.Information = 0;

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPostReadBuffers:            %wZ Invalid user buffer, oldB=%p, status=%x\n",
					&p2pCtx->VolCtx->Name,
					origBuf,
					Data->IoStatus.Status));
		}

	}
	finally{

		//
		//  If we are supposed to, cleanup the allocated memory and release
		//  the volume context.  The freeing of the MDL (if there is one) is
		//  handled by FltMgr.
		//

		if (cleanupAllocatedBuffer) {

			LOG_PRINT(LOGFL_READ,
				("SwapBuffers!SwapPostReadBuffers:            %wZ newB=%p info=%d Freeing\n",
					&p2pCtx->VolCtx->Name,
					p2pCtx->SwappedBuffer,
					Data->IoStatus.Information));

			ExFreePool(p2pCtx->SwappedBuffer);
			FltReleaseContext(p2pCtx->VolCtx);

			ExFreeToNPagedLookasideList(&Pre2PostContextList,
				p2pCtx);
		}
	}

	return retValue;
}

FLT_POSTOP_CALLBACK_STATUS
SwapPostReadBuffersWhenSafe(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
)
/*++

Routine Description:

We had an arbitrary users buffer without a MDL so we needed to get
to a safe IRQL so we could lock it and then copy the data.

Arguments:

Data - Pointer to the filter callbackData that is passed to us.

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance, its associated volume and
file object.

CompletionContext - Contains state from our PreOperation callback

Flags - Denotes whether the completion is successful or is being drained.

Return Value:

FLT_POSTOP_FINISHED_PROCESSING - This is always returned.

--*/
{
	PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;
	PPRE_2_POST_CONTEXT p2pCtx = CompletionContext;
	PVOID origBuf;
	NTSTATUS status;

	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	ASSERT(Data->IoStatus.Information != 0);

	//
	//  This is some sort of user buffer without a MDL, lock the user buffer
	//  so we can access it.  This will create a MDL for it.
	//

	DbgPrint("\n PostReadwhensafe 0x%08x : 0x%08x", iopb->MinorFunction, iopb->IrpFlags);

	status = FltLockUserBuffer(Data);

	if (iopb->IrpFlags & IRP_PAGING_IO)
	{
		DbgPrint("\n PostRead IRP : 0x%08x", iopb->IrpFlags);
	}



	if (!NT_SUCCESS(status)) {

		LOG_PRINT(LOGFL_ERRORS,
			("SwapBuffers!SwapPostReadBuffersWhenSafe:    %wZ Could not lock user buffer, oldB=%p, status=%x\n",
				&p2pCtx->VolCtx->Name,
				iopb->Parameters.Read.ReadBuffer,
				status));

		//
		//  If we can't lock the buffer, fail the operation
		//

		Data->IoStatus.Status = status;
		Data->IoStatus.Information = 0;

	}
	else {

		//
		//  Get a system address for this buffer.
		//

		origBuf = MmGetSystemAddressForMdlSafe(iopb->Parameters.Read.MdlAddress,
			NormalPagePriority);

		if (origBuf == NULL) {

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPostReadBuffersWhenSafe:    %wZ Failed to get system address for MDL: %p\n",
					&p2pCtx->VolCtx->Name,
					iopb->Parameters.Read.MdlAddress));

			//
			//  If we couldn't get a SYSTEM buffer address, fail the operation
			//

			Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
			Data->IoStatus.Information = 0;

		}
		else {

			//
			//  Copy the data back to the original buffer.  Note that we
			//  don't need a try/except because we will always have a system
			//  buffer address.
			//

			RtlCopyMemory(origBuf,
				p2pCtx->SwappedBuffer,
				Data->IoStatus.Information);

			//DbgPrint("\n SwapPostReadBuffersWhenSafe [%d][%d]",iopb->Parameters.Read.Length,iopb->Parameters.Read.ByteOffset);
		}
	}

	//
	//  Free allocated memory and release the volume context
	//

	LOG_PRINT(LOGFL_READ,
		("SwapBuffers!SwapPostReadBuffersWhenSafe:    %wZ newB=%p info=%d Freeing\n",
			&p2pCtx->VolCtx->Name,
			p2pCtx->SwappedBuffer,
			Data->IoStatus.Information));

	ExFreePool(p2pCtx->SwappedBuffer);
	FltReleaseContext(p2pCtx->VolCtx);

	ExFreeToNPagedLookasideList(&Pre2PostContextList,
		p2pCtx);

	return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS
PostWriteBuffers(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
)
/*++

Routine Description:


Arguments:


Return Value:

--*/
{
	PPRE_2_POST_CONTEXT p2pCtx = CompletionContext;

	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);

	LOG_PRINT(LOGFL_WRITE,
		("SwapBuffers!SwapPostWriteBuffers:           %wZ newB=%p info=%d Freeing\n",
			&p2pCtx->VolCtx->Name,
			p2pCtx->SwappedBuffer,
			Data->IoStatus.Information));

	//
	//  Free allocate POOL and volume context
	//

	ExFreePool(p2pCtx->SwappedBuffer);
	FltReleaseContext(p2pCtx->VolCtx);

	ExFreeToNPagedLookasideList(&Pre2PostContextList,
		p2pCtx);

	return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS
PostDirCtrlBuffers(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
)
/*++

Routine Description:

This routine does the post Directory Control buffer swap handling.

Arguments:

This routine does postRead buffer swap handling
Data - Pointer to the filter callbackData that is passed to us.

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance, its associated volume and
file object.

CompletionContext - The completion context set in the pre-operation routine.

Flags - Denotes whether the completion is successful or is being drained.

Return Value:

FLT_POSTOP_FINISHED_PROCESSING
FLT_POSTOP_MORE_PROCESSING_REQUIRED

--*/
{
	PVOID origBuf;
	PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;
	FLT_POSTOP_CALLBACK_STATUS retValue = FLT_POSTOP_FINISHED_PROCESSING;
	PPRE_2_POST_CONTEXT p2pCtx = CompletionContext;
	BOOLEAN cleanupAllocatedBuffer = TRUE;

	//
	//  Verify we are not draining an operation with swapped buffers
	//

	ASSERT(!FlagOn(Flags, FLTFL_POST_OPERATION_DRAINING));

	try {

		//
		//  If the operation failed or the count is zero, there is no data to
		//  copy so just return now.
		//

		if (!NT_SUCCESS(Data->IoStatus.Status) ||
			(Data->IoStatus.Information == 0)) {

			LOG_PRINT(LOGFL_DIRCTRL,
				("SwapBuffers!SwapPostDirCtrlBuffers:         %wZ newB=%p No data read, status=%x, info=%x\n",
					&p2pCtx->VolCtx->Name,
					p2pCtx->SwappedBuffer,
					Data->IoStatus.Status,
					Data->IoStatus.Information));

			leave;
		}

		//DbgPrint("\n PostDir 0x%08x : 0x%08x",iopb->MinorFunction,iopb->IrpFlags);

		//
		//  We need to copy the read data back into the users buffer.  Note
		//  that the parameters passed in are for the users original buffers
		//  not our swapped buffers
		//

		if (iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress != NULL) {

			//
			//  There is a MDL defined for the original buffer, get a
			//  system address for it so we can copy the data back to it.
			//  We must do this because we don't know what thread context
			//  we are in.
			//

			origBuf = MmGetSystemAddressForMdlSafe(iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress,
				NormalPagePriority);

			if (origBuf == NULL) {

				LOG_PRINT(LOGFL_ERRORS,
					("SwapBuffers!SwapPostDirCtrlBuffers:         %wZ Failed to get system address for MDL: %p\n",
						&p2pCtx->VolCtx->Name,
						iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress));

				//
				//  If we failed to get a SYSTEM address, mark that the
				//  operation failed and return.
				//

				Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
				Data->IoStatus.Information = 0;
				leave;
			}

		}
		else if (FlagOn(Data->Flags, FLTFL_CALLBACK_DATA_SYSTEM_BUFFER) ||
			FlagOn(Data->Flags, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION)) {

			//
			//  If this is a system buffer, just use the given address because
			//      it is valid in all thread contexts.
			//  If this is a FASTIO operation, we can just use the
			//      buffer (inside a try/except) since we know we are in
			//      the correct thread context.
			//

			origBuf = iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer;

		}
		else {

			//
			//  They don't have a MDL and this is not a system buffer
			//  or a fastio so this is probably some arbitrary user
			//  buffer.  We can not do the processing at DPC level so
			//  try and get to a safe IRQL so we can do the processing.
			//

			if (FltDoCompletionProcessingWhenSafe(Data,
				FltObjects,
				CompletionContext,
				Flags,
				SwapPostDirCtrlBuffersWhenSafe,
				&retValue)) {

				//
				//  This operation has been moved to a safe IRQL, the called
				//  routine will do (or has done) the freeing so don't do it
				//  in our routine.
				//

				cleanupAllocatedBuffer = FALSE;

			}
			else {

				//
				//  We are in a state where we can not get to a safe IRQL and
				//  we do not have a MDL.  There is nothing we can do to safely
				//  copy the data back to the users buffer, fail the operation
				//  and return.  This shouldn't ever happen because in those
				//  situations where it is not safe to post, we should have
				//  a MDL.
				//

				LOG_PRINT(LOGFL_ERRORS,
					("SwapBuffers!SwapPostDirCtrlBuffers:         %wZ Unable to post to a safe IRQL\n",
						&p2pCtx->VolCtx->Name));

				Data->IoStatus.Status = STATUS_UNSUCCESSFUL;
				Data->IoStatus.Information = 0;
			}

			leave;
		}

		//
		//  We either have a system buffer or this is a fastio operation
		//  so we are in the proper context.  Copy the data handling an
		//  exception.
		//
		//  NOTE:  Due to a bug in FASTFAT where it is returning the wrong
		//         length in the information field (it is sort) we are always
		//         going to copy the original buffer length.
		//

		try {

			RtlCopyMemory(origBuf,
				p2pCtx->SwappedBuffer,
				/*Data->IoStatus.Information*/
				iopb->Parameters.DirectoryControl.QueryDirectory.Length);

		} except(EXCEPTION_EXECUTE_HANDLER) {

			Data->IoStatus.Status = GetExceptionCode();
			Data->IoStatus.Information = 0;

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPostDirCtrlBuffers:         %wZ Invalid user buffer, oldB=%p, status=%x, info=%x\n",
					&p2pCtx->VolCtx->Name,
					origBuf,
					Data->IoStatus.Status,
					Data->IoStatus.Information));
		}

	}
	finally{

		//
		//  If we are supposed to, cleanup the allocate memory and release
		//  the volume context.  The freeing of the MDL (if there is one) is
		//  handled by FltMgr.
		//

		if (cleanupAllocatedBuffer) {

			LOG_PRINT(LOGFL_DIRCTRL,
				("SwapBuffers!SwapPostDirCtrlBuffers:         %wZ newB=%p info=%d Freeing\n",
					&p2pCtx->VolCtx->Name,
					p2pCtx->SwappedBuffer,
					Data->IoStatus.Information));

			ExFreePool(p2pCtx->SwappedBuffer);
			FltReleaseContext(p2pCtx->VolCtx);

			ExFreeToNPagedLookasideList(&Pre2PostContextList,
				p2pCtx);
		}
	}

	return retValue;
}

FLT_POSTOP_CALLBACK_STATUS
SwapPostDirCtrlBuffersWhenSafe(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__in PVOID CompletionContext,
	__in FLT_POST_OPERATION_FLAGS Flags
)
/*++

Routine Description:

We had an arbitrary users buffer without a MDL so we needed to get
to a safe IRQL so we could lock it and then copy the data.

Arguments:

Data - Pointer to the filter callbackData that is passed to us.

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance, its associated volume and
file object.

CompletionContext - The buffer we allocated and swapped to

Flags - Denotes whether the completion is successful or is being drained.

Return Value:

FLT_POSTOP_FINISHED_PROCESSING - This is always returned.

--*/
{
	PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;
	PPRE_2_POST_CONTEXT p2pCtx = CompletionContext;
	PVOID origBuf;
	NTSTATUS status;

	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	ASSERT(Data->IoStatus.Information != 0);

	//
	//  This is some sort of user buffer without a MDL, lock the
	//  user buffer so we can access it
	//

	status = FltLockUserBuffer(Data);

	//DbgPrint("\n PostDirWhenSafe 0x%08x : 0x%08x",iopb->MinorFunction,iopb->IrpFlags);

	if (!NT_SUCCESS(status)) {

		LOG_PRINT(LOGFL_ERRORS,
			("SwapBuffers!SwapPostDirCtrlBuffersWhenSafe: %wZ Could not lock user buffer, oldB=%p, status=%x\n",
				&p2pCtx->VolCtx->Name,
				iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer,
				status));

		//
		//  If we can't lock the buffer, fail the operation
		//

		Data->IoStatus.Status = status;
		Data->IoStatus.Information = 0;

	}
	else {

		//
		//  Get a system address for this buffer.
		//

		origBuf = MmGetSystemAddressForMdlSafe(iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress,
			NormalPagePriority);

		if (origBuf == NULL) {

			LOG_PRINT(LOGFL_ERRORS,
				("SwapBuffers!SwapPostDirCtrlBuffersWhenSafe: %wZ Failed to get System address for MDL: %p\n",
					&p2pCtx->VolCtx->Name,
					iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress));

			//
			//  If we couldn't get a SYSTEM buffer address, fail the operation
			//

			Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
			Data->IoStatus.Information = 0;

		}
		else {

			//
			//  Copy the data back to the original buffer
			//
			//  NOTE:  Due to a bug in FASTFAT where it is returning the wrong
			//         length in the information field (it is short) we are
			//         always going to copy the original buffer length.
			//

			RtlCopyMemory(origBuf,
				p2pCtx->SwappedBuffer,
				/*Data->IoStatus.Information*/
				iopb->Parameters.DirectoryControl.QueryDirectory.Length);
		}
	}

	//
	//  Free the memory we allocated and return
	//

	LOG_PRINT(LOGFL_DIRCTRL,
		("SwapBuffers!SwapPostDirCtrlBuffersWhenSafe: %wZ newB=%p info=%d Freeing\n",
			&p2pCtx->VolCtx->Name,
			p2pCtx->SwappedBuffer,
			Data->IoStatus.Information));

	ExFreePool(p2pCtx->SwappedBuffer);
	FltReleaseContext(p2pCtx->VolCtx);

	ExFreeToNPagedLookasideList(&Pre2PostContextList,
		p2pCtx);

	return FLT_POSTOP_FINISHED_PROCESSING;
}