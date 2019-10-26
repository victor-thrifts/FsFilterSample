

BOOL EnumHandle(DWORD pid)
{
	HMODULE hNtdll = 0;
	ULONG hNum = 0,ret = 0;//hNum ZwQueryInformationProcess return
	NTSTATUS state = 0;
	HANDLE TageHandle = 0;
	HANDLE hTest = 0;
	ULONG i = 0;
	POBJECT_NAME_INFORMATION ObjName;
	HANDLE hProcess = 0;
	ULONG RetVal = 0;
	POBJECT_TYPE_INFORMATION lpIn;
	ULONG  DupFaNum = 0; //To record the error of the copy handle and to determine the cycle of death.
	ZWQUERYINFORMATIONPROCESS ZwQueryInformationProcess;
	ZWQUERYOBJECT ZwQueryObject;

	hNtdll = GetModuleHandle("ntdll.dll");
	if(hNtdll == NULL)
	{
		printf("hNtdll is null\n");
		return FALSE;
	}

	ZwQueryInformationProcess = (ZWQUERYINFORMATIONPROCESS)GetProcAddress(hNtdll,"ZwQueryInformationProcess");
	ZwQueryObject = (ZWQUERYOBJECT)GetProcAddress(hNtdll,"ZwQueryObject");
	RtlAdjustPrivilege = (RTLADJUSTPRIVILEGE)GetProcAddress(hNtdll,"RtlAdjustPrivilege");

	if(ZwQueryInformationProcess == NULL || ZwQueryObject == NULL || RtlAdjustPrivilege == NULL)
	{
		printf("ZwAPI is null\n");
		return FALSE;
	}

	RtlAdjustPrivilege(20,1,0,&RetVal);//debug
	RtlAdjustPrivilege(19,1,0,&RetVal);

	hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
                            PROCESS_DUP_HANDLE, FALSE, pid);
	if(hProcess==0)
	{
		printf("OpenProcess is null;\n");
		return FALSE;
	}
	
	ObjName = (POBJECT_NAME_INFORMATION)malloc(0x2000);
	if(ObjName == 0)
	{
		printf("malloc is null\n");
		CloseHandle(hProcess);
		return FALSE;
	}
	state = ZwQueryInformationProcess(hProcess,ProcessHandleCount,&hNum,sizeof(hNum),&ret);
	if(!NT_SUCCESS(state))
	{
		printf("state is null\n");
		free(ObjName);
		CloseHandle(hProcess);
		return FALSE;
	}
	
	lpIn = (POBJECT_TYPE_INFORMATION)malloc(0x1000);
	if(lpIn==0)
	{
		free(ObjName);
		CloseHandle(hProcess);
		return FALSE;
	}

	while (i != hNum)
	{
		TageHandle = (HANDLE)((ULONG)TageHandle + 4);
		if(DuplicateHandle(hProcess,TageHandle,GetCurrentProcess(),&hTest,0,FALSE,DUPLICATE_SAME_ACCESS))
		{
			state = ZwQueryObject(hTest,ObjectTypeInformation,lpIn,0x1000,NULL);
			if(!NT_SUCCESS(state))
			{
				CloseHandle(hProcess);
				free(ObjName);
				free(lpIn);
				return FALSE;
			}
			wprintf(L"ObjectType:%wZ---",lpIn->TypeName);
			if(!wcscmp((wchar_t*)lpIn->TypeName.Buffer,L"Key"))
			{
				//printf("Find Key\n");
			}
			state = ZwQueryObject(hTest,ObjectNameInformation,ObjName,0x2000,NULL);
			if(!NT_SUCCESS(state))
			{
				i++;
				continue;
			}
			printf("Handle:0x%x---",(ULONG)TageHandle);
			wprintf(L"HandleName:%wZ\n",ObjName->Name);
			memset(ObjName,0,0x2000);
			i++;
		}else
		{
			DupFaNum++;
		}
		if(DupFaNum>50)//Can only judge the number of failures to avoid the death cycle
			break;

	}

	//EtwRegistration这个类型复制不过来 不知道怎么回事
	printf("\nReal HandleCount:%d---Enum HandleCount:%d\n",hNum,i);
	printf("EtwRegistration Handle Num:%d\n",hNum - i);
	free(ObjName);
	free(lpIn);
	CloseHandle(hProcess);
	return TRUE;
}

