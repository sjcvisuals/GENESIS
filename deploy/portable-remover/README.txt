GENESIS Remover — portable package
==================================

What this is
------------
A self-contained ExtGenesisRemover build you can copy to another Windows machine
and install into whichever PIXERA build that machine uses.

Contents
--------
  INSTALL.bat          <- right-click, Run as administrator
  install.ps1          <- path picker + copy into PIXERA
  README.txt           <- this file
  package\             <- ExtGenesisRemover.dll + MatteHost + ORT + assets

Install on the target machine
-----------------------------
1. Quit PIXERA completely (Task Manager: no Pixera / PixeraEngine / MatteHost).
2. Unzip this folder anywhere (Desktop is fine).
3. Right-click INSTALL.bat -> Run as administrator.
4. Pick the PIXERA root when prompted:
     - Choose a detected install, or
     - Browse / type the folder that contains data\rxExt
       Example: C:\Program Files\AV Stumpfl\Pixera\build_25-3_INTER_73
5. In PIXERA, browse / re-import:
     <PIXERA>\data\rxExt\ExtGenesisRemover\ExtGenesisRemover.dll
   Do NOT use ExtGenesis.dll. Do NOT point at Documents\GitHub\...\out\.

Non-interactive (scripted) install
----------------------------------
  powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 ^
    -PixeraRoot "C:\Program Files\AV Stumpfl\Pixera\build_25-3_INTER_73"

Verify
------
  Debug Status / %TEMP%\ExtGenesisRemover.log stamp must match BUILD_STAMP.txt
  inside package\ (remover-demo-v63f3-*). Expect shm=1 matte=1 mismatch=0.

Logs
----
  %TEMP%\genesis-install-remover-portable.log
  %TEMP%\ExtGenesisRemover.log
  %TEMP%\GenesisRemoverMatteHost.log
