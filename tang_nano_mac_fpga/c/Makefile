all: warning mac_command1 mac_command

warning:
	@echo xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
	@echo Building both mac_command and mac_command1. Be VERY sure
	@echo to use the correct one.  See the README.
	@echo xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

mac_command1: mac_command1.c
	gcc -o mac_command1 mac_command1.c -lpigpio -lrt

mac_command: mac_command.c
	gcc -o mac_command mac_command.c -lpigpio -lrt

clean:
	rm -f mac_command mac_command1

