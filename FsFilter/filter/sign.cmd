makecert -r -pe -ss PrivateCertStore -n CN=ghtx.com(Test) -eku 1.3.6.1.5.5.7.3.3 fsfilter.cer -sv fsfilter.pvk
pvk2pfx.exe /pvk fsfilter.pvk /spc fsfilter.cer /pfx fsfilter.pfx

CertMgr /add filter.cer /s /r localMachine root
CertMgr /add filter.cer /s /r localMachine trustedpublisher

stampinf -f fsfilter.inf -d 09/01/2019 -v 1.0.0
Inf2cat.exe /driver:objchk_win7_amd64\amd64 /os:7_X64


Signtool sign /v /fd sha256 /s root /n ghtx.com(Test) /t http://timestamp.digicert.com fsfilter.cat
Signtool sign /v /fd sha256 /s root /n ghtx.com(Test) /t http://timestamp.digicert.com fsfilter.sys
