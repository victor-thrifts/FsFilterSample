/*++

Copyright (c) 1989-2002  Microsoft Corporation

Module Name:

    mspyUser.c

Abstract:

    This file contains the implementation for the main function of the
    user application piece of MiniSpy.  This function is responsible for
    controlling the command mode available to the user to control the
    kernel mode driver.

Environment:

    User mode

--*/

#include <DriverSpecs.h>
__user_code

#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <assert.h>
#include "mspyLog.h"
#include <strsafe.h>

#define SUCCESS              0
#define USAGE_ERROR          1
#define EXIT_INTERPRETER     2
#define EXIT_PROGRAM         4

#define INTERPRETER_EXIT_COMMAND1 "go"
#define PROGRAM_EXIT_COMMAND      "exit"
#define CMDLINE_SIZE              256
#define NUM_PARAMS                40

#define MINISPY_NAME            L"fsFilter"

HANDLE gport = INVALID_HANDLE_VALUE;

typedef struct _VolumeDosList{
  struct _VolumeDosList *head;
  WCHAR DosName[16];
  WCHAR VolumeName[32];  
} VolumeDosList, *PVolumeDosList;

PVolumeDosList vDosList = NULL;

DWORD
InterpretCommand (
    __in int argc,
    __in_ecount(argc) char *argv[],
    __in PLOG_CONTEXT Context
    );

DWORD
RetrieveCmd(
     __in PCOMMAND_MESSAGE pcommandMessage,
    __deref_out_bcount_part(BUFFER_SIZE/sizeof( PVOID ), *bytesReturned) PLOG_RECORD* ppLogRecord, 
    __out LPDWORD lpbytesReturned)
/*++

Routine Description:

    This runs for get the spacis command to talk with the filter.

Arguments:

    pcommandMessage - Contains command and data context which size in reserved.
    pLogRecord  -  return buffer
    lpbytesReturned -  return size

Return Value:

    The thread successfully terminated

--*/
{
    PVOID alignedBuffer[BUFFER_SIZE/sizeof( PVOID )];
    HRESULT hResult;
    PRECORD_DATA pRecordData;
    PLOG_RECORD pLogRecord;
 
    hResult = FilterSendMessage( gport,
                                    pcommandMessage,
                                    pcommandMessage->Reserved,
                                    alignedBuffer,
                                    sizeof(alignedBuffer),
                                    lpbytesReturned );

    if (IS_ERROR( hResult )) {

        if (HRESULT_FROM_WIN32( ERROR_INVALID_HANDLE ) == hResult) {

            printf( "The kernel component of minispy has unloaded. Exiting\n" );
            ExitProcess( 0 );
        } else {

            if (hResult != HRESULT_FROM_WIN32( ERROR_NO_MORE_ITEMS )) {

                printf( "UNEXPECTED ERROR received: %x\n", hResult );
            }
        }
    }

    pLogRecord = (PLOG_RECORD) alignedBuffer;
    pLogRecord->RecordType = RECORD_TYPE_NORMAL;

    if( pLogRecord->Reserved == -1 ) return (DWORD)-1;   //failed execute the command.

    //
    //  Logic to write record to screen and/or file
    //

    if (pLogRecord->Length > *lpbytesReturned) {

        printf( "UNEXPECTED LOG_RECORD size: used=%d bytesReturned=%d\n",
                pLogRecord->Length,
                *lpbytesReturned);
        return (DWORD)-1;
    }

    pRecordData = &pLogRecord->Data;

    //
    //  See if a reparse point entry
    //
    
    if (FlagOn(pLogRecord->RecordType,RECORD_TYPE_FILETAG)) {

        if (!TranslateFileTag( pLogRecord )){

            //
            // If this is a reparse point that can't be interpreted, move on.
            //

            pLogRecord = (PLOG_RECORD)Add2Ptr(pLogRecord,pLogRecord->Length);
        }
    }

    pLogRecord->Name[(pLogRecord->Length - sizeof(LOG_RECORD))/2] = UNICODE_NULL;

    ScreenDump( pLogRecord->SequenceNumber,
                pLogRecord->Name,
                pRecordData );

    //
    //  The RecordType could also designate that we are out of memory
    //  or hit our program defined memory limit, so check for these
    //  cases.
    //

    if (FlagOn(pLogRecord->RecordType,RECORD_TYPE_FLAG_OUT_OF_MEMORY)) {

            printf( "M:  %08X System Out of Memory\n",
                    pLogRecord->SequenceNumber );

    } else if (FlagOn(pLogRecord->RecordType,RECORD_TYPE_FLAG_EXCEED_MEMORY_ALLOWANCE)) {

            printf( "M:  %08X Exceeded Mamimum Allowed Memory Buffers\n",
                    pLogRecord->SequenceNumber );
    }

    pLogRecord = HeapAlloc(GetProcessHeap(), 0, *lpbytesReturned);
    RtlCopyMemory(pLogRecord, alignedBuffer, *lpbytesReturned);
    *ppLogRecord = pLogRecord;

    printf( "Log: Command send ok\n" );
    return 0;
}

#pragma warning(disable: 4172)

PCHAR
getProtectionFolder()
{
    PLOG_RECORD pLogRecord;

    PCOMMAND_MESSAGE pcommandMessage;

    DWORD bytesReturned = 0;
    DWORD bufferlen;

    ULONG ret;

    static UCHAR buffer[512] = {0};

    pcommandMessage = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ROUND_TO_SIZE( sizeof(COMMAND_MESSAGE) + sizeof(UNICODE_NULL), sizeof(PVOID)));

    pcommandMessage->Command = GetMiniSpyProtectionFolder;

    pcommandMessage->Reserved = ROUND_TO_SIZE( sizeof(COMMAND_MESSAGE) + sizeof(UNICODE_NULL), sizeof(PVOID));

    RetrieveCmd(pcommandMessage, &pLogRecord, &bytesReturned);

    assert( bytesReturned >= pLogRecord->Length );

    bufferlen = (pLogRecord->Length - sizeof(LOG_RECORD))/2;

    assert(bufferlen < 512 );
    
    ret = WideCharToMultiByte(CP_ACP, 0, pLogRecord->Name, bufferlen, buffer, 512, NULL, NULL);

    if(ret == 0) {
        
        ret = GetLastError();

        printf("WideCharToMultiBytes Faield with code: %x", ret);
   
    }
    else
    {
        buffer[bufferlen] = '\0'; 

        printf("The current protection floder is:  %s\n",  buffer);
    }

    HeapFree(GetProcessHeap(), 0, pcommandMessage);

    HeapFree(GetProcessHeap(), 0, pLogRecord);

	return buffer;
}

PVOID 
setProtectionFolder(WCHAR* protectionFloder)
{
    PLOG_RECORD pLogRecord = NULL;

    PCOMMAND_MESSAGE pcommandMessage;

    DWORD bytesReturned = 0;

    pcommandMessage = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ROUND_TO_SIZE( sizeof(COMMAND_MESSAGE) + wcslen(protectionFloder)*2 + sizeof(UNICODE_NULL), sizeof(PVOID)));

    pcommandMessage->Command = SetMiniSpyProtectionFolder;
    pcommandMessage->Reserved = ROUND_TO_SIZE( sizeof(COMMAND_MESSAGE) + wcslen(protectionFloder)*2 + sizeof(UNICODE_NULL), sizeof(PVOID));

    RtlCopyMemory(
    &pcommandMessage->Data[0],
    protectionFloder,
    wcslen(protectionFloder)*2
    );

    RetrieveCmd(pcommandMessage, &pLogRecord, &bytesReturned);  

    if(pLogRecord->Data.Reserved ==0)

        printf("Set Protection Floder successful!\n");
    
    HeapFree(GetProcessHeap(), 0, pcommandMessage);

    HeapFree(GetProcessHeap(), 0, pLogRecord);  
	return NULL;
}


PVOID 
setOpenProcess(WCHAR* proc)
{
    PLOG_RECORD pLogRecord = NULL;

    PCOMMAND_MESSAGE pcommandMessage;

    DWORD bytesReturned = 0;

    pcommandMessage = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ROUND_TO_SIZE( sizeof(COMMAND_MESSAGE) + wcslen(proc)*2 + sizeof(UNICODE_NULL), sizeof(PVOID)));

    pcommandMessage->Command = SetMiniSpyOpenProccess;
    pcommandMessage->Reserved = ROUND_TO_SIZE( sizeof(COMMAND_MESSAGE) + wcslen(proc)*2 + sizeof(UNICODE_NULL), sizeof(PVOID));

    RtlCopyMemory(
    &pcommandMessage->Data[0],
    proc,
    wcslen(proc)*2
    );

    RetrieveCmd(pcommandMessage, &pLogRecord, &bytesReturned);  

    if(pLogRecord->Data.Reserved ==0)

        printf("Set Protection Floder successful!\n");
    
    HeapFree(GetProcessHeap(), 0, pcommandMessage);

    HeapFree(GetProcessHeap(), 0, pLogRecord);  
	return NULL;
}

VOID
DisplayError (
   __in DWORD Code
   )

/*++

Routine Description:

   This routine will display an error message based off of the Win32 error
   code that is passed in. This allows the user to see an understandable
   error message instead of just the code.

Arguments:

   Code - The error code to be translated.

Return Value:

   None.

--*/

{
    __nullterminated WCHAR buffer[MAX_PATH] = { 0 }; 
    DWORD count;
    HMODULE module = NULL;
    HRESULT status;

    count = FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
                           NULL,
                           Code,
                           0,
                           buffer,
                           sizeof(buffer) / sizeof(WCHAR),
                           NULL);


    if (count == 0) {

        count = GetSystemDirectory( buffer,
                                    sizeof(buffer) / sizeof( WCHAR ) );

        if (count==0 || count > sizeof(buffer) / sizeof( WCHAR )) {

            //
            //  In practice we expect buffer to be large enough to hold the 
            //  system directory path. 
            //

            printf("    Could not translate error: %d\n", Code);
            return;
        }


        status = StringCchCat( buffer,
                               sizeof(buffer) / sizeof( WCHAR ),
                               L"\\fltlib.dll" );

        if (status != S_OK) {

            printf("    Could not translate error: %d\n", Code);
            return;
        }

        module = LoadLibraryExW( buffer, NULL, LOAD_LIBRARY_AS_DATAFILE );

        //
        //  Translate the Win32 error code into a useful message.
        //

        count = FormatMessage (FORMAT_MESSAGE_FROM_HMODULE,
                               module,
                               Code,
                               0,
                               buffer,
                               sizeof(buffer) / sizeof(WCHAR),
                               NULL);

        if (module != NULL) {

            FreeLibrary( module );
        }

        //
        //  If we still couldn't resolve the message, generate a string
        //

        if (count == 0) {

            printf("    Could not translate error: %d\n", Code);
            return;
        }
    }

    //
    //  Display the translated error.
    //

    printf("    %ws\n", buffer);
}


//
//  Main uses a loop which has an assignment in the while 
//  conditional statement. Suppress the compiler's warning.
//

#pragma warning(push)
#pragma warning(disable:4706) // assignment within conditional expression

#ifndef __DLL_EXPORT__

int _cdecl
main (
    __in int argc,
    __in_ecount(argc) char *argv[]
    )
/*++

Routine Description:

    Main routine for minispy

Arguments:

Return Value:

--*/
{
    HRESULT hResult = S_OK;
    DWORD result;
    ULONG threadId;
    HANDLE thread = NULL;
    LOG_CONTEXT context;
    CHAR inputChar;

    //
    //  Initialize handle in case of error
    //

    context.ShutDown = NULL;

    //
    //  Open the port that is used to talk to
    //  MiniSpy.
    //

    printf( "Connecting to filter's port...\n" );

    hResult = FilterConnectCommunicationPort( MINISPY_PORT_NAME,
                                              0,
                                              NULL,
                                              0,
                                              NULL,
                                              &gport );

    if (IS_ERROR( hResult )) {

        printf( "Could not connect to filter: 0x%08x\n", hResult );
        DisplayError( hResult );
        goto Main_Exit;
    }

    //
    // Initialize the fields of the LOG_CONTEXT
    //

    context.Port = gport;
    context.ShutDown = CreateSemaphore( NULL,
                                        0,
                                        1,
                                        L"MiniSpy shut down" );
    context.CleaningUp = FALSE;
    context.LogToFile = FALSE;
    context.LogToScreen = FALSE;        //don't start logging yet
    context.NextLogToScreen = TRUE;
    context.OutputFile = NULL;

    if (context.ShutDown == NULL) {

        result = GetLastError();
        printf( "Could not create semaphore: %d\n", result );
        DisplayError( result );
        goto Main_Exit;
    }

    //
    // Check the valid parameters for startup
    //

    if (argc > 1) {

        if (InterpretCommand( argc - 1, &(argv[1]), &context ) == USAGE_ERROR) {

            goto Main_Exit;
        }
    }

    //
    // Create the thread to read the log records that are gathered
    // by MiniSpy.sys.
    //
    printf( "Creating logging thread...\n" );
    thread = CreateThread( NULL,
                           0,
                           RetrieveLogRecords,
                           (LPVOID)&context,
                           0,
                           &threadId);

    if (!thread) {

        result = GetLastError();
        printf( "Could not create logging thread: %d\n", result );
        DisplayError( result );
        goto Main_Exit;
    }

    //
    // Check to see what devices we are attached to from
    // previous runs of this program.
    //

    ListDevices();

    //
    //  Process commands from the user
    //

    printf( "\nHit [Enter] to begin command mode...\n\n" );
    fflush( stdout );

    //
    //  set screen logging state
    //

    context.LogToScreen = context.NextLogToScreen;

    while (inputChar = (CHAR)getchar()) {

        CHAR *parms[NUM_PARAMS];
        CHAR commandLine[CMDLINE_SIZE+1];
        INT parmCount, count;
        DWORD returnValue = SUCCESS;
        BOOL newParm;
        CHAR ch;

        if (inputChar == '\n') {

            //
            // Start command interpreter.  First we must turn off logging
            // to screen if we are.  Also, remember the state of logging
            // to the screen, so that we can reinstate that when command
            // interpreter is finished.
            //

            context.NextLogToScreen = context.LogToScreen;
            context.LogToScreen = FALSE;

            while (returnValue != EXIT_INTERPRETER) {

                //
                // Print prompt
                //
                printf( ">" );

                //
                // Read in next line, keeping track of the number of parameters
                // as we go.
                //

                parmCount = 0;
                newParm = TRUE;
                for ( count = 0;
                      (count < CMDLINE_SIZE) && ((ch = (CHAR)getchar()) != '\n');
                      count++)
                {
                    commandLine[count] = ch;

                    if (newParm && (ch != ' ')) {

                        parms[parmCount++] = &commandLine[count];
                    }

                    if (parmCount >= NUM_PARAMS) {

                        break;
                    }

                    //
                    //  Always insert NULL's for spaces
                    //

                    if (ch == ' ') {

                        newParm = TRUE;
                        commandLine[count] = 0;

                    } else {

                        newParm = FALSE;
                    }
                }

                commandLine[count] = '\0';

                if (parmCount == 0) {

                    continue;
                }

                //
                // We've got our parameter count and parameter list, so
                // send it off to be interpreted.
                //

                returnValue = InterpretCommand( parmCount, parms, &context );

                if (returnValue == EXIT_PROGRAM) {

                    // Time to stop the program
                    goto Main_Cleanup;
                }
            }

            //
            // Set LogToScreen appropriately based on any commands seen
            //

            context.LogToScreen = context.NextLogToScreen;

            if (context.LogToScreen) {

                printf( "Should be logging to screen...\n" );
            }
        }
    }

Main_Cleanup:

    //
    // Clean up the threads, then fall through to Main_Exit
    //

    printf( "Cleaning up...\n" );

    //
    // Set the Cleaning up flag to TRUE to notify other threads
    // that we are cleaning up
    //
    context.CleaningUp = TRUE;

    //
    // Wait for everyone to shut down
    //

    WaitForSingleObject( context.ShutDown, INFINITE );

    if (context.LogToFile) {

        fclose( context.OutputFile );
    }

Main_Exit:

    //
    // Clean up the data that is always around and exit
    //

    if(context.ShutDown) {

        CloseHandle( context.ShutDown );
    }

    if (thread) {

        CloseHandle( thread );
    }

    if (INVALID_HANDLE_VALUE != gport) {
        CloseHandle( gport );
    }
    return 0;
}

#endif  //__DLL_EXPORT__

#pragma warning(pop)

DWORD
InterpretCommand (
    __in int argc,
    __in_ecount(argc) char *argv[],
    __in PLOG_CONTEXT Context
    )
/*++

Routine Description:

    Process options from the user

Arguments:

Return Value:

--*/
{
    LONG parmIndex;
    PCHAR parm;
    HRESULT hResult;
    DWORD returnValue = SUCCESS;
    WCHAR buffer[BUFFER_SIZE/2];
    DWORD bufferLength;
    PWCHAR instanceString;
    WCHAR instanceName[INSTANCE_NAME_MAX_CHARS + 1];

    //
    // Interpret the command line parameters
    //
    for (parmIndex = 0; parmIndex < argc; parmIndex++) {

        parm = argv[parmIndex];

        if (parm[0] == '/') {

            //
            // Have the beginning of a switch
            //

            switch (parm[1]) {

            case 'a':
            case 'A':

                //
                // Attach to the specified drive letter
                //

                parmIndex++;

                if (parmIndex >= argc) {

                    //
                    // Not enough parameters
                    //

                    goto InterpretCommand_Usage;
                }

                parm = argv[parmIndex];

                printf( "    Attaching to %s... ", parm );

                bufferLength = MultiByteToWideChar( CP_ACP,
                                                    MB_ERR_INVALID_CHARS,
                                                    parm,
                                                    -1,
                                                    (LPWSTR)buffer,
                                                    BUFFER_SIZE/sizeof( WCHAR ) );

                if (bufferLength == 0) {

                    //
                    //  We do not expect the user to provide a parm that
                    //  causes buffer to overflow. 
                    //

                    goto InterpretCommand_Usage;
                }

                hResult = FilterAttach( MINISPY_NAME,
                                        (PWSTR)buffer,
                                        NULL, // instance name
                                        sizeof( instanceName ),
                                        instanceName );

                if (SUCCEEDED( hResult )) {

                    printf( "    Instance name: %S\n", instanceName );

                } else {

                    printf( "\n    Could not attach to device: 0x%08x\n", hResult );
                    DisplayError( hResult );
                    returnValue = SUCCESS;
                }

                break;

            case 'd':
            case 'D':

                //
                // Detach to the specified drive letter
                //

                parmIndex++;

                if (parmIndex >= argc) {

                    //
                    // Not enough parameters
                    //

                    goto InterpretCommand_Usage;
                }

                parm = argv[parmIndex];

                printf( "    Detaching from %s\n", parm );
                bufferLength = MultiByteToWideChar( CP_ACP,
                                                    MB_ERR_INVALID_CHARS,
                                                    parm,
                                                    -1,
                                                    (LPWSTR)buffer,
                                                    BUFFER_SIZE/sizeof( WCHAR ) );

                if (bufferLength == 0) {

                    //
                    //  We do not expect the user to provide a parm that
                    //  causes buffer to overflow.
                    //

                    goto InterpretCommand_Usage; 
                }

                //
                //  Get the next argument to see if it is an InstanceId
                //

                parmIndex++;

                if (parmIndex >= argc) {

                    instanceString = NULL;

                } else {

                    if (argv[parmIndex][0] == '/') {

                        //
                        //  This is just the next command, so don't
                        //  internet it as the InstanceId.
                        //

                        instanceString = NULL;
                        parmIndex--;

                    } else {

                        parm = argv[parmIndex];
                        bufferLength = MultiByteToWideChar( CP_ACP,
                                                            MB_ERR_INVALID_CHARS,
                                                            parm,
                                                            -1,
                                                            (LPWSTR)instanceName,
                                                            sizeof( instanceName )/sizeof( WCHAR ) );

                        if (bufferLength == 0) {

                            //
                            //  We do not expect the user to provide a parm that
                            //  causes buffer to overflow.
                            //

                            goto InterpretCommand_Usage;
                        }

                        instanceString = instanceName;
                    }
                }

                //
                //  Detach from the volume and instance specified.
                //

                hResult = FilterDetach( MINISPY_NAME,
                                        (PWSTR)buffer,
                                        instanceString );

                if (IS_ERROR( hResult )) {

                    printf( "    Could not detach from device: 0x%08x\n", hResult );
                    DisplayError( hResult );
                    returnValue = SUCCESS;
                }
                break;

            case 'l':
            case 'L':

                //
                // List all devices that are currently being monitored
                //

                ListDevices();
                break;

            case 'n':
            case 'N':

                //
                // Output logging results to screen, save new value to
                // instate when command interpreter is exited.
                //
                if (Context->NextLogToScreen) {

                    printf( "    Turning off logging to screen\n" );

                } else {

                    printf( "    Turning on logging to screen\n" );
                }

                Context->NextLogToScreen = !Context->NextLogToScreen;
                break;

            case 'f':
            case 'F':

                //
                // Output logging results to file
                //

                if (Context->LogToFile) {

                    printf( "    Stop logging to file \n" );
                    Context->LogToFile = FALSE;
                    assert( Context->OutputFile );
                    fclose( Context->OutputFile );
                    Context->OutputFile = NULL;

                } else {

                    parmIndex++;

                    if (parmIndex >= argc) {

                        //
                        // Not enough parameters
                        //

                        goto InterpretCommand_Usage;
                    }

                    parm = argv[parmIndex];
                    printf( "    Log to file %s\n", parm );
                    Context->OutputFile = fopen( parm, "w" );
                    assert( Context->OutputFile );
                    Context->LogToFile = TRUE;
                }
                break;
            
            case 's':
            case 'S':
                {
                    CHAR dir[MAX_PATH];
                    ULONG len;
                    //
                    //  set Protection folder.
                    //
                    parmIndex++;

                    if (parmIndex >= argc) {

                        //
                        // Not enough parameters
                        //

                        goto InterpretCommand_Usage;
                    }

                    parm = dir; 
                    len = MAX_PATH;
                    for( parmIndex; parmIndex < argc; parmIndex++)
                    {
                        strncpy_s(parm, len, argv[parmIndex], strlen(argv[parmIndex]));
                        parm += strlen(parm);
                        len -= strlen(parm);
                        if(argc > parmIndex+1){
                            strncpy_s(parm, len, " ", 1);
                            parm++;
                        }
                    }

                    *parm = '\0';
                    parm = dir;            

                    printf( " Setting Protection floder: %s\n", parm );

                    bufferLength = MultiByteToWideChar( CP_ACP,
                                                        MB_ERR_INVALID_CHARS,
                                                        parm,
                                                        -1,
                                                        (LPWSTR)buffer,
                                                        BUFFER_SIZE/sizeof( WCHAR ) );

                    if (bufferLength == 0) {

                        //
                        //  We do not expect the user to provide a parm that
                        //  causes buffer to overflow.
                        //

                        goto InterpretCommand_Usage; 
                    }

                    setProtectionFolder(buffer);
                }
                break;

            case 'g':
            case 'G':
                //
                //  get Protection folder.
                //    
                getProtectionFolder();
                
                break;

            case 'e':
            case 'E':
                {
                    //
                    //  set Protection folder.
                    //
                    parmIndex++;

                    if (parmIndex >= argc) {

                        //
                        // Not enough parameters
                        //

                        goto InterpretCommand_Usage;
                    }

                    parm = argv[parmIndex];

                    printf( " Setting Proccess Name: %s to accesss then protction floder.\n ", parm );

                    bufferLength = MultiByteToWideChar( CP_ACP,
                                                        MB_ERR_INVALID_CHARS,
                                                        parm,
                                                        -1,
                                                        (LPWSTR)buffer,
                                                        BUFFER_SIZE/sizeof( WCHAR ) );

                    if (bufferLength == 0) {

                        //
                        //  We do not expect the user to provide a parm that
                        //  causes buffer to overflow.
                        //

                        goto InterpretCommand_Usage; 
                    }

                    setOpenProcess(buffer);
                }

                break;

            default:

                //
                // Invalid switch, goto usage
                //
                goto InterpretCommand_Usage;
            }

        } else {

            //
            // Look for "go" to see if we should exit interpreter
            //

            if (!_strnicmp( parm,
                            INTERPRETER_EXIT_COMMAND1,
                            sizeof( INTERPRETER_EXIT_COMMAND1 ))) {

                returnValue = EXIT_INTERPRETER;
                goto InterpretCommand_Exit;
            }

            //
            // Look for "exit" to see if we should exit program
            //

            if (!_strnicmp( parm,
                            PROGRAM_EXIT_COMMAND,
                            sizeof( PROGRAM_EXIT_COMMAND ))) {

                returnValue = EXIT_PROGRAM;
                goto InterpretCommand_Exit;
            }

            //
            // Invalid parameter
            //
            goto InterpretCommand_Usage;
        }
    }

InterpretCommand_Exit:
    return returnValue;

InterpretCommand_Usage:
    printf("Valid switches: [/a <drive>] [/d <drive>] [/l] [/s] [/f [<file name>]]\n"
           "    [/a <drive>] starts monitoring <drive>\n"
           "    [/d <drive> [<instance id>]] detaches filter <instance id> from <drive>\n"
           "    [/l] lists all the drives the monitor is currently attached to\n"
           "    [/s] turns on and off showing logging output on the screen\n"
           "    [/f [<file name>]] turns on and off logging to the specified file\n"
           "    [/e <proccess>] set proccess to access the protection folder.\n"
           "    [/g] get the protection floder. \n"
           "    [/s <dirname>] set protection floder"
           "  If you are in command mode:\n"
           "    [enter] will enter command mode\n"
           "    [go] will exit command mode\n"
           "    [exit] will terminate this program\n"
           );
    returnValue = USAGE_ERROR;
    goto InterpretCommand_Exit;
}


ULONG
IsAttachedToVolume(
    __in LPCWSTR VolumeName
    )
/*++

Routine Description:

    Determine if our filter is attached to this volume

Arguments:

    VolumeName - The volume we are checking

Return Value:

    TRUE - we are attached
    FALSE - we are not attached (or we couldn't tell)

--*/
{
    PWCHAR filtername;
    CHAR buffer[1024];
    PINSTANCE_FULL_INFORMATION data = (PINSTANCE_FULL_INFORMATION)buffer;
    HANDLE volumeIterator = INVALID_HANDLE_VALUE;
    ULONG bytesReturned;
    ULONG instanceCount = 0;
    HRESULT hResult;

    //
    //  Enumerate all instances on this volume
    //

    hResult = FilterVolumeInstanceFindFirst( VolumeName,
                                             InstanceFullInformation,
                                             data,
                                             sizeof(buffer)-sizeof(WCHAR),
                                             &bytesReturned,
                                             &volumeIterator );

    if (IS_ERROR( hResult )) {

        return instanceCount;
    }

    do {

        assert((data->FilterNameBufferOffset+data->FilterNameLength) <= (sizeof(buffer)-sizeof(WCHAR)));
        __analysis_assume((data->FilterNameBufferOffset+data->FilterNameLength) <= (sizeof(buffer)-sizeof(WCHAR)));

        //
        //  Get the name.  Note that we are NULL terminating the buffer
        //  in place.  We can do this because we don't care about the other
        //  information and we have guaranteed that there is room for a NULL
        //  at the end of the buffer.
        //


        filtername = Add2Ptr(data,data->FilterNameBufferOffset);
        filtername[data->FilterNameLength/sizeof( WCHAR )] = L'\0';

        //
        //  Bump the instance count when we find a match
        //

        if (_wcsicmp(filtername,MINISPY_NAME) == 0) {

            instanceCount++;
        }

    } while (SUCCEEDED( FilterVolumeInstanceFindNext( volumeIterator,
                                                                  InstanceFullInformation,
                                                                  data,
                                                                  sizeof(buffer)-sizeof(WCHAR),
                                                                  &bytesReturned ) ));

    //
    //  Close the handle
    //

    FilterVolumeInstanceFindClose( volumeIterator );
    return instanceCount;
}


void FreevDosList()
{
	PVolumeDosList tmp;
	while (vDosList){
		tmp = vDosList;
		vDosList = vDosList->head;
		HeapFree(GetProcessHeap(), 0, tmp);
	}
}


void
ListDevices(
    VOID
    )
/*++

Routine Description:

    Display the volumes we are attached to

Arguments:

Return Value:

--*/
{
    UCHAR buffer[1024];
    PFILTER_VOLUME_BASIC_INFORMATION volumeBuffer = (PFILTER_VOLUME_BASIC_INFORMATION)buffer;
    HANDLE volumeIterator = INVALID_HANDLE_VALUE;
    ULONG volumeBytesReturned;
    HRESULT hResult = S_OK;
    __nullterminated WCHAR driveLetter[15] = { 0 };
    ULONG instanceCount;
	PVolumeDosList tmpvDosList = NULL;

    __try {

        //
        //  Find out size of buffer needed
        //

        hResult = FilterVolumeFindFirst( FilterVolumeBasicInformation,
                                         volumeBuffer,
                                         sizeof(buffer)-sizeof(WCHAR),   //save space to null terminate name
                                         &volumeBytesReturned,
                                         &volumeIterator );

        if (IS_ERROR( hResult )) {

             __leave;
        }

        assert( INVALID_HANDLE_VALUE != volumeIterator );

        //
        //  Output the header
        //

        printf( "\n"
                "Dos Name        Volume Name                            Status \n"
                "--------------  ------------------------------------  --------\n" );

        //
        //  Loop through all of the filters, displaying instance information
        //
		
		if (vDosList) FreevDosList();

        do {

            assert((FIELD_OFFSET(FILTER_VOLUME_BASIC_INFORMATION,FilterVolumeName) + volumeBuffer->FilterVolumeNameLength) <= (sizeof(buffer)-sizeof(WCHAR)));
            __analysis_assume((FIELD_OFFSET(FILTER_VOLUME_BASIC_INFORMATION,FilterVolumeName) + volumeBuffer->FilterVolumeNameLength) <= (sizeof(buffer)-sizeof(WCHAR)));

            volumeBuffer->FilterVolumeName[volumeBuffer->FilterVolumeNameLength/sizeof( WCHAR )] = UNICODE_NULL;

            instanceCount = IsAttachedToVolume(volumeBuffer->FilterVolumeName);

			vDosList = HeapAlloc(GetProcessHeap(), 0, sizeof(VolumeDosList));

			if (SUCCEEDED(FilterGetDosName(volumeBuffer->FilterVolumeName, driveLetter, sizeof(driveLetter) / sizeof(WCHAR))))
			{
				wcscpy_s(vDosList->DosName, 16, driveLetter);
                vDosList->DosName[2] = UNICODE_NULL;
			}
			else{
				wcscpy_s(vDosList->DosName, 16, L"");
                vDosList->DosName[15] = UNICODE_NULL;
			}
            
			wcscpy_s(vDosList->VolumeName, 32, volumeBuffer->FilterVolumeName);
            vDosList->VolumeName[31] = UNICODE_NULL;


			printf("%-14ws  %-36ws  %s",
				vDosList->DosName,
				vDosList->VolumeName,
				(instanceCount > 0) ? "Attached" : "");

			vDosList->head = tmpvDosList;
			tmpvDosList = vDosList;
			

            if (instanceCount > 1) {

                printf( " (%d)\n", instanceCount );

            } else {

                printf( "\n" );
            }

        } while (SUCCEEDED( hResult = FilterVolumeFindNext( volumeIterator,
                                                                        FilterVolumeBasicInformation,
                                                                        volumeBuffer,
                                                                        sizeof(buffer)-sizeof(WCHAR),    //save space to null terminate name
                                                                        &volumeBytesReturned ) ));

		vDosList = tmpvDosList;

        if (HRESULT_FROM_WIN32( ERROR_NO_MORE_ITEMS ) == hResult) {

            hResult = S_OK;
        }

    } __finally {

        if (INVALID_HANDLE_VALUE != volumeIterator) {

            FilterVolumeFindClose( volumeIterator );
        }

        if (IS_ERROR( hResult )) {

            if (HRESULT_FROM_WIN32( ERROR_NO_MORE_ITEMS ) == hResult) {

                printf( "No volumes found.\n" );

            } else {

                printf( "Volume listing failed with error: 0x%08x\n",
                        hResult );
            }
        }
    }
}


