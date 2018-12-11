projeto_SO: projeto_SO.o drone_movement.o   
	gcc projeto_SO.o drone_movement.o -lpthread -D_REENTRANT -Wall -g -lm -o project_exe
projeto_SO.o: projeto_SO.c drone_movement.h   
	gcc -c projeto_SO.c 
drone_movement.o: drone_movement.c drone_movement.h  
	gcc -c drone_movement.c

