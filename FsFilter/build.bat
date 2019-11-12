copy /Y user\mspyLog.c 		userdll\mspyLog.c
copy /Y user\mspyLog.h 		userdll\mspyLog.h
copy /Y user\mspyUser.c 	userdll\mspyUser.c
copy /Y user\mspyUser.rc	userdll\mspyUser.rc
build.exe -cZ
copy /Y E:\WorkSpace\FsFilter1\filter\objchk_wxp_x86\i386\fsFilter.pdb E:\WorkSpace\FsFilter\filter\objchk_wxp_x86\i386\fsFilter.pdb
copy /Y E:\WorkSpace\FsFilterSample\FsFilter\userdll\objchk_wxp_x86\i386\MINISPY.dll E:\WorkSpace\typescript\nodesvr32\cpp\
copy /Y E:\WorkSpace\FsFilterSample\FsFilter\userdll\objchk_wxp_x86\i386\MINISPY.lib E:\WorkSpace\typescript\nodesvr32\cpp\
copy /Y E:\WorkSpace\FsFilterSample\FsFilter\userdll\objchk_wxp_x86\i386\MINISPY.pdb E:\WorkSpace\typescript\nodesvr32\cpp\