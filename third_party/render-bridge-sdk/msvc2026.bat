
cd /d %~dp0
cmake -B _build -G "Visual Studio 18 2026"
start _build/RXExt.slnx
