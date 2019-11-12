//  THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
//  ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
//  PARTICULAR PURPOSE.
//
//  Copyright  1997 - 2003  Microsoft Corporation.  All Rights Reserved.
//
//  FILE:	intrface.cpp
//
//  PURPOSE:	Implement the UI interface plug-in
// 

#include <DriverSpecs.h>
__user_code

#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <assert.h>
#include "mspyLog.h"
#include <strsafe.h>
#include <process.h>

#ifdef __DLL_EXPORT__
#pragma warning(disable: 4189)
#pragma warning(disable: 4101)


////////////////////////////////////////////////////////
//      Internal Globals
////////////////////////////////////////////////////////

HINSTANCE ghInstance = NULL;

LOG_CONTEXT context;


///////////////////////////////////////////////////////////
//
// Exported functions
//

int
mainprocess()
{
    HRESULT hResult = S_OK;
    DWORD result;
    ULONG threadId;
    HANDLE thread = NULL;
    
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
        Sleep(5000);
        ExitProcess(0);
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
    context.LogToScreen = context.NextLogToScreen;

    context.CleaningUp = FALSE;  

    if (context.ShutDown == NULL) {

        result = GetLastError();
        printf( "Could not create semaphore: %d\n", result );
    }

    return 0;
}

STDAPI GetRecords()
{
    return RetrieveLogRecords((LPVOID)&context);
}

BOOL WINAPI DllMain(HINSTANCE hInst, WORD wReason, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(lpReserved);

    switch(wReason)
    {
        case DLL_PROCESS_ATTACH:
            // VERBOSE(DLLTEXT("Process attach.\r\n"));

            // Save DLL instance for use later.
            ghInstance = hInst;
            mainprocess();
            break;

        case DLL_THREAD_ATTACH:
            // VERBOSE(DLLTEXT("Thread attach.\r\n"));
            break;

        case DLL_PROCESS_DETACH:
            // VERBOSE(DLLTEXT("Process detach.\r\n"));
            break;

        case DLL_THREAD_DETACH:
            // VERBOSE(DLLTEXT("Thread detach.\r\n"));
            break;
    }

    return TRUE;
}

// Can DLL unload now?
//
STDAPI DllCanUnloadNow()
{
    //
    // To avoid leaving OEM DLL still in memory when Unidrv or Pscript drivers
    // are unloaded, Unidrv and Pscript driver ignore the return value of
    // DllCanUnloadNow of the OEM DLL, and always call FreeLibrary on the OEMDLL.
    //
    // If OEM DLL spins off a working thread that also uses the OEM DLL, the
    // thread needs to call LoadLibrary and FreeLibraryAndExitThread, otherwise
    // it may crash after Unidrv or Pscript calls FreeLibrary.
    //
        if(context.ShutDown)
        {
            ReleaseSemaphore( context.ShutDown, 1, NULL );
        }
        return S_OK ;
       // return S_FALSE;
}


#endif // __DLL_EXPORT__

