@echo off
rem One unattended hour of the child: the practice worlds, then every ARC-AGI-3 game.
rem Run by the scheduled task "Ciall child growth".
cd /d C:\Users\USER\ciall..suvstrate
.build\grow.exe 24 child
"C:\Users\USER\AppData\Local\Programs\Python\Python314\python.exe" arc\grow_arc.py
