@echo off
rem One unattended hour of the child: the practice worlds, then every ARC-AGI-3 game.
rem Run by the scheduled task "Ciall child growth".
cd /d C:\Users\USER\ciall..suvstrate
.build\grow.exe 24 child
rem it reads itself: what it is made of goes to child\self_map.txt
.build\self_map.exe certifiable-c\smarsh_core.c certifiable-c\smarsh_core.h certifiable-c\smarsh_ending.c certifiable-c\smarsh_ending.h certifiable-c\smarsh_explore.c certifiable-c\smarsh_explore.h certifiable-c\smarsh_self.h certifiable-c\smarsh_play.c certifiable-c\smarsh_grow.c certifiable-c\play_arc.c certifiable-c\self_map.c > child\self_map.txt
"C:\Users\USER\AppData\Local\Programs\Python\Python314\python.exe" arc\grow_arc.py
rem it learns to understand stories, choosing what it is curious about
if exist booksabi_train.txt .build\grasp_books.exe --curious 300
if not exist child\self_write.off "C:\Users\USER\AppData\Local\Programs\Python\Python314\python.exe" child\self_write.py
