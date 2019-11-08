REM Value Description 
REM 0 System provided INF. 
REM 128 Set the default path of the installation to the location of the INF. This is the typical setting. 
REM +0 Never reboot the computer. 
REM +1 Reboot the computer in all cases. 
REM +2 Always ask the users if they want to reboot. 
REM +3 Reboot the computer if necessary without asking user for permission. 
REM +4 If a reboot of the computer is necessary, ask the user for permission before rebooting. 

RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection DefaultInstall 132 FsFilter.inf