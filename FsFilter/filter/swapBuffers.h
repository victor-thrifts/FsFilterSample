/*++

Copyright (c) 1999 - 2002  Microsoft Corporation

Module Name:

SwapBuffers.c

Abstract:

This is a sample filter which demonstrates proper access of data buffer
and a general guideline of how to swap buffers.
For now it only swaps buffers for:

IRP_MJ_READ
IRP_MJ_WRITE
IRP_MJ_DIRECTORY_CONTROL

By default this filter attaches to all volumes it is notified about.  It
does support having multiple instances on a given volume.

Environment:

Kernel mode

--*/

#ifndef __SWAP_BUFFERS_STANDALONE_C		
//#define __SWAP_BUFFERS_STANDALONE_C	
//#define SwapDriverEntry DriverEntry
#endif //__SWAP_BUFFERS_STANDALONE_C
	

/*************************************************************************
Prototypes
*************************************************************************/
NTSTATUS
SwapInstanceSetup(
__in PCFLT_RELATED_OBJECTS FltObjects,
__in FLT_INSTANCE_SETUP_FLAGS Flags,
__in DEVICE_TYPE VolumeDeviceType,
__in FLT_FILESYSTEM_TYPE VolumeFilesystemType
);

VOID
SwapCleanupVolumeContext(
__in PFLT_CONTEXT Context,
__in FLT_CONTEXT_TYPE ContextType
);

NTSTATUS
SwapInstanceQueryTeardown(
__in PCFLT_RELATED_OBJECTS FltObjects,
__in FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
);

DRIVER_INITIALIZE SwapDriverEntry;
NTSTATUS
SwapDriverEntry(
__in PDRIVER_OBJECT DriverObject,
__in PUNICODE_STRING RegistryPath
);

NTSTATUS
SwapFilterUnload(
__in FLT_FILTER_UNLOAD_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
SwapPreReadBuffers(
__inout PFLT_CALLBACK_DATA Data,
__in PCFLT_RELATED_OBJECTS FltObjects,
__deref_out_opt PVOID *CompletionContext
);

FLT_POSTOP_CALLBACK_STATUS
SwapPostReadBuffers(
__inout PFLT_CALLBACK_DATA Data,
__in PCFLT_RELATED_OBJECTS FltObjects,
__in PVOID CompletionContext,
__in FLT_POST_OPERATION_FLAGS Flags
);

FLT_POSTOP_CALLBACK_STATUS
SwapPostReadBuffersWhenSafe(
__inout PFLT_CALLBACK_DATA Data,
__in PCFLT_RELATED_OBJECTS FltObjects,
__in PVOID CompletionContext,
__in FLT_POST_OPERATION_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
SwapPreDirCtrlBuffers(
__inout PFLT_CALLBACK_DATA Data,
__in PCFLT_RELATED_OBJECTS FltObjects,
__deref_out_opt PVOID *CompletionContext
);

FLT_POSTOP_CALLBACK_STATUS
SwapPostDirCtrlBuffers(
__inout PFLT_CALLBACK_DATA Data,
__in PCFLT_RELATED_OBJECTS FltObjects,
__in PVOID CompletionContext,
__in FLT_POST_OPERATION_FLAGS Flags
);

FLT_POSTOP_CALLBACK_STATUS
SwapPostDirCtrlBuffersWhenSafe(
__inout PFLT_CALLBACK_DATA Data,
__in PCFLT_RELATED_OBJECTS FltObjects,
__in PVOID CompletionContext,
__in FLT_POST_OPERATION_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
SwapPreWriteBuffers(
__inout PFLT_CALLBACK_DATA Data,
__in PCFLT_RELATED_OBJECTS FltObjects,
__deref_out_opt PVOID *CompletionContext
);

FLT_POSTOP_CALLBACK_STATUS
SwapPostWriteBuffers(
__inout PFLT_CALLBACK_DATA Data,
__in PCFLT_RELATED_OBJECTS FltObjects,
__in PVOID CompletionContext,
__in FLT_POST_OPERATION_FLAGS Flags
);

VOID
SwapReadDriverParameters(
__in PUNICODE_STRING RegistryPath
);
