; os80.asm - Disk Operating System for the teensy80
;
;
; (some of this comes from my earlier work from 1986 on)
; (C) k theis  1986-2021 <theis.kurt@gmail.com>
; MIT license applies to this source code only
; (just note that I'm the original author in any derivative works)

; commands:	
; halt		jump to monitor
; mon		start ml monitor
; load		load a file from disk to buffer
; save		save buffer to disk
; dir		show the disk directory
; del		delete a file on the SD card
; run		execute program in memory at 0x00
; print		send ascii data in ram buffer to serial printer
; help		show commands

; TODO: 
; run [filename] - load file/execute in single step
; loadhex [filename] - load an intel .hex format file to ram
; savehex [filename] - save ram to intel .hex format file
; recv - receive data from serial terminal (xmodem?) to ram
; send - send ram data to serial port (xmodem?)
; better help display (details, maybe a disk-based help command)

; ***** NOTES *****
; Set your terminal so the backspace key sends DEL (0x7f)
;
; *****************


;; System equates
CR		EQU	0x0d
LF		EQU	0x0a
BS		EQU	0x08		; backspace (^H)
DEL		EQU	0x7f		; delete
BELL		EQU	0x07		; BELL
EOL		EQU	0xff		; end of line for DB/print
NUL		EQU	0x00		; used in printer routine
CONOUT		EQU	0x11
CONIN		EQU	0x11
CONIN_STAT	EQU	0x10		; return 0 until serial input is ready
DISK		EQU	0x20		; disk subsystem address
PRINTER		EQU	0x12		; serial printer port


;; define addresses used

; program starts at 0xf000 (top 4K of memory space)
; (NOTE: some of these *addresses are used in drivers.h - keep them in sync!)
PROGSTART	EQU	0xf000		; start of OS/80 program code
; these are in RAM under the main program
BUFSTART	EQU	0xeffe		; *2 byte hi:lo start address
BUFEND		EQU	0xeffc		; *2 byte hi:lo end address
BUFTEMP		EQU	0xeffa		; 2 byte hi:lo buffer
; unused 	EQU	0xeff8		; 2 byte
; unused	EQU	0xeff6		; 2 byte
; unused	EQU	0xeff4		; 2 byte
; unused	EQU	0xeff2		; 2 byte
; unused	EQU	0xeff0		; 2 byte
LINEIN		EQU	0xef9e		; 82 byte linein buffer (80 bytes + 2 guard)
FILENAME	EQU	0xef4c		; *16 byte filename
TMPNAME		EQU	0xef3c		; 16 byte temp buffer
; 0xef2c is an 80 byte guard between the buffers and the stack.
STACK		EQU	0xeedc		; stack pointer



;-----------------------------------
;--------- Start Of Program --------
;-----------------------------------

		org 0		; OS/80 resides in hi mem so that loading and saving
				; can work in lo mem giving more working space for the 
				; users programs.
		jmp coldstart






;--------------------------------
;---------- Main Loop -----------
;--------------------------------

		org PROGSTART		; this is in hi mem

coldstart	; jump here at beginning 
		lxi h	signon_msg	; show sign-on
		call	print		; display on conout

warmstart	lxi sp	STACK		; near top of memory

		mvi a	0
test1a		push psw
		mvi a	CR
		out	CONOUT
		mvi a	LF
		out	CONOUT
		pop	psw
		push 	psw
		call	printdecimal
		pop	psw
		inr a
		cpi 100
		jnz	test1a



; Command Loop - show prompt, get line, interpret or save line		
loop1	call	prompt		; show prompt
		call	getline		; get a line of text from the console
		lxi h	LINEIN		; test for <cr> or \n as 1st char
		mov a,m			
		cpi		LF
		jz	loop1
		cpi		CR
		jz	loop1
		; test line entered for commands
		call	cmdtest		; test entered string, run command if match
		cpi 	0x00		; return value 0x00 is SUCCESS, any other is a failure
		jz		loop1
		; any other number is an error
		lxi h	errormsg
		call	print		; show error
		jmp	loop1			; get another command
		

;------------------
signon_msg	DB CR DB LF DB "*** OS/80 Command Interpeter ***"
		DB CR DB LF
		DB "type 'help' for commands" DB EOL

errormsg	DB CR DB LF DB "EH?" DB EOL



;----------------------------------
;-------- Subroutines -------------
;----------------------------------


;-------------------------
;--------- Print ---------
;-------------------------

print	; display a message pointed to by HL, terminate by EOL
		mov a,m
		cpi		EOL
		rz		; done - return
		out		CONOUT
		inx h
		jmp	print



;--------------------------
;--------- Prompt ---------
;--------------------------

prompt	; display a prompt
		mvi a	CR			; show prompt on console
		out	CONOUT
		mvi	a	LF
		out	CONOUT
		mvi a	'O'
		out	CONOUT
		mvi a	'K'
		out	CONOUT
		mvi a	'>'
		out	CONOUT
		ret


;------------------------------
;------ Ascii to Binary -------
;------------------------------

a2b		mov a,b			; convert 2 digit ascii in BC to binary in A
		sbi	30h
		cpi	0bh
		jc	a2b_1
		sbi	7
a2b_1	ana a
		rlc
		rlc
		rlc
		rlc
		ani	f0h
		mov b,a
		mov a,c			; restore a
		sbi	30h
		cpi	0bh
		jc	a2b_2
		sbi	7
a2b_2	ani	0fh
		ora b
		ret

;------------------------------------
;--------- Binary to Ascii ----------
;------------------------------------

printascii	; convert binary in A, print as 2 hex bytes
		mov b,a		; save A
		ani	f0h	; MSB
		rar
		rar
		rar
		rar
		adi	30h
		cpi	3Ah
		jc	printascii_1
		adi	7
printascii_1
		out	CONOUT
		mov a,b		; LSB
		ani	0fh
		adi	30h
		cpi	3Ah
		jc	printascii_2
		adi	7
printascii_2
		out	CONOUT
		ret


;--------------------------------------
;----------- Print Decimal  -----------
;--------------------------------------

printdecimal	; convert value in A to decimal, print it
		mov l,a			; put value in L
		mvi h	0x00		; MSB is 0
		mvi b	0x0a		; dividing by 10D
		call	divide
		mov a,l			; get quotient
		adi 0x30		; to ascii
		out	CONOUT
		mov a,h			; get remainder
		adi 0x30		; to ascii
		out	CONOUT
		ret


	

		
;--------------------------------------------
;------------ Divide by 10 ------------------

divide		; divide number in HL by B. Dividend in HL, divisor in B
		mvi c 0x08		; counter
up		dad h
		mov a,h
		sub b
		jc down
		mov h,a
		inr l
down		dcr c
		jnz up			; ie: 59D / 10D L=5, H=9
		ret			; result in HL (remainder/quotient)




;---------------------------------
;----------- Getline -------------
;---------------------------------

getline	; get a line of text from the console. Terminates with \n (LF) or \r (CR)
	; DEL deletes chars, ^U deletes the line and starts over

		lxi h	LINEIN		; start of line buffer
		
		; clear the buffer
		mvi c	0x50		; buffer length (80dec)
gl0		mvi a	0x00
		mov m,a			; set buffer to 0
		inx h			; next mem location
		dcr c			; counter
		mvi a	0x00
		cmp c
		jnz	gl0
		
		; get a line of input
		mvi c	0x01		; max 80 chars
		lxi h	LINEIN		; re-point to start of buffer

gl1		in	CONIN_STAT
		cpi	0x00		; (blocking input)
		jz	gl1		; wait until serial port has chars
		;
gl2		in	CONIN		; get a char from the console
		
		; test special chars
		cpi	0x03		; ctrl-C
		jz	loop1		; exit input routine, return to OK prompt
		cpi	0x15		; ctrl-U
		jz	discard_line
		cpi	DEL		; Delete last char
		jnz	gl2a
		
		; backspace - delete last char
		mvi a	0x01
		cmp c			; don't skip past the beginning
		jz	gl1		; else ignore the backspace
		mvi a	BS		; back up cursor
		out	CONOUT
		mvi a	' '		; delete on screen
		out	CONOUT
		mvi a	BS
		out	CONOUT		; and back up cursor
		dcx h			; prev mem location
		dcr c			; running counter
		jmp	gl1		; resume getting chars
		
gl2a					
		mov b,a			; save char
		mvi a	0x50
		cmp c
		jz	gl3
		mov a,b			; restore char just entered
		mov m,a			; save to linein buffer
		out	CONOUT		; and echo character
		cpi	LF
		rz
		cpi	CR
		rz			; return on LF
		inx h			; inc pointer
		inr c			; and char counter
		jmp	gl1		; loop forever

		;
gl3		; buffer full - ring bell, don't save current char
		mvi a	BELL		; ring console bell
		out	CONOUT
		jmp	gl1		; continue, waiting for LF or CR to exit routine

discard_line	; ctrl-U received - ignore line, start over
		mvi a	' '
		out	CONOUT
		mvi a	'^'
		out	CONOUT
		mvi a	'U'
		out	CONOUT		; show ' ^U' to indicate what the user did
		mvi a	CR		; start new line
		out	CONOUT
		mvi a	LF
		out	CONOUT
		jmp	getline		; start over


;-------------------------------------
;------------ Get File Name ----------
;-------------------------------------

getFilename	; strip the filename from the LINEIN buffer
		; filename is after the current command space separated

		inx h		; skip past final char of the command that brought us here
		;
		lxi b		FILENAME	; buffer to store filename
gf1		mov a,m				; get a char from buffer
		cpi	CR
		jz	get_filename_error	; no filename
		cpi	LF
		jz	get_filename_error	
		cpi	EOL
		jz	get_filename_error
		cpi	' '			; check for leading spaces
		jnz	gf2			; got a real char
		inx h				; point to next char
		jmp	gf1

gf2		; read char for filename
		stax b				; save char
		inx h
		inx b
		mov a,m				; get next char
		cpi	CR
		jz	gf3			; end of filename
		cpi	LF
		jz	gf3
		jmp	gf2			; finish loading filename
	
gf3		; filename loaded, put \0 after filename
		mvi a	0x00
		stax b
		ret			; done - return w/filename loaded

get_filename_error	; missing or bad filename: show error and return
		lxi h	gfn_message1
		call	print
		mvi a	0x01		; return error code
		ret

gfn_message1	DB CR  DB LF  DB "Missing filename"  DB EOL


;----------------------------------
;-------- Get Address -------------
;----------------------------------

getAddress	; get 4 digit address from LINEIN, return in BUFTEMP HI:LO

		; 1st test if LININ is 4 digits by counting entered digits
		mvi c	0x00
		lxi h   LINEIN
gsa0		mov a,m
		cpi	LF
		jz	gsa1
		cpi	CR
		jz	gsa1
		inx h
		inr c
		jmp	gsa0		; count until CR/LF

gsa1		mvi a 0x04
		cmp c			; look for proper count
		jz	gsa2		; got it
		lxi h	gsa_bad_address
		call	print
		mvi a	0x01		; return code
		ret			; go no farther
				
gsa2		lxi h	LINEIN
		mov b,m			; get MSB
		inx h
		mov c,m
		call	a2b		; return value in A
		lxi d	BUFTEMP
		stax d			; save MSB
		inx h			; get LSB
		mov b,m
		inx h
		mov c,m
		call	a2b		; convert it
		inx d
		stax d			; should have new value in BUFSTART
		mvi a 0x00		; return code
		ret

gsa_bad_address DB CR  DB LF DB "Value must be 4 digit hex (ie. FA30)" DB EOL

;-----------------------------------
;---------- Command Tests ----------
;-----------------------------------

cmdtest		; command interpreter: look for command names

test1	; halt		halt the processor, jump to hardware monitor routine
		lxi h	LINEIN
		mvi a	'h'
		cmp m
		jnz test2
		inx h
		mvi a	'a'
		cmp m
		jnz	test2
		inx h
		mvi a	'l'
		cmp m
		jnz test2
		inx h
		mvi a	't'
		cmp m
		jnz test2
		hlt		; command is 'halt'


test2	; dir		display disk directory
		lxi h	LINEIN		; point to start of line
		mvi a	'd'
		cmp m
		jnz	test3
		inx h
		mvi a	'i'
		cmp m
		jnz	test3
		inx h
		mvi a	'r'
		cmp m
		jnz	test3
		jmp	showDir


test3	; load [filename]		load a file from disk to memory buffer
		lxi h	LINEIN	; point to start of line
		mvi a	'l'
		cmp	m
		jnz	test4
		inx h
		mvi a	'o'
		cmp m
		jnz	test4
		inx h
		mvi a	'a'
		cmp	m
		jnz	test4
		inx h
		mvi a	'd'
		cmp m
		jnz	test4
		inx h
		mvi a	' '		; separator between 'load' and filename
		cmp	m
		jnz	test4
		jmp loadfile	; load [filename]



test4	; save [filename]		; save memory buffer to disk	
		lxi h	LINEIN
		mvi a	's'
		cmp m
		jnz	test5
		inx h
		mvi a	'a'
		cmp m
		jnz	test5
		inx h
		mvi a	'v'
		cmp m
		jnz	test5
		inx h
		mvi a	'e'
		cmp m
		jnz	test5
		inx h
		mvi a	' '
		cmp m
		jnz	test5
		jmp	savefile ; save [filename]
		

test5	; help		; show command summary
		lxi h	LINEIN
		mvi a	'h'
		cmp m
		jnz	test6
		inx h
		mvi a	'e'
		cmp m
		jnz	test6
		inx h
		mvi a	'l'
		cmp	m
		jnz	test6
		inx h
		mvi a	'p'
		cmp	m
		jnz	test6
		jmp	showhelp	; show help summary

test6	; run		- start execution at 0x0000
		lxi h	LINEIN
		mvi a	'r'
		cmp m
		jnz	test7
		inx h
		mvi a	'u'
		cmp m
		jnz	test7
		inx h
		mvi a	'n'
		cmp m
		jnz	test7
		jmp	0x0000		;; jump to reset point


test7	; delete	- delete a file on the SD card
		lxi h	LINEIN
		mvi a	'd'
		cmp m
		jnz	test8
		inx h
		mvi a	'e'
		cmp m
		jnz	test8
		inx h
		mvi a	'l'
		cmp m
		jnz	test8
		jmp	delete		; delete a file

test8	; mon		ml monitor
		lxi h	LINEIN
		mvi a	'm'
		cmp m
		jnz	test9
		inx h
		mvi a	'o'
		cmp m
		jnz	test9
		inx h
		mvi a	'n'
		cmp m
		jnz	test9
		jmp	mon		; run monitor program		

test9		; print - send ascii bytes to printer port
		lxi h	LINEIN
		mvi a	'p'
		cmp m
		jnz	test10
		inx h
		mvi a	'r'
		cmp m
		jnz	test10
		inx h
		mvi a	'i'
		cmp m
		jnz	test10
		inx h
		mvi a	'n'
		cmp m
		jnz	test10
		inx h
		mvi a	't'
		cmp m
		jnz	test10
		jmp	printFile	; send ascii file to serial printer


test10		; output current real time to console
		lxi h	LINEIN
		mvi a	't'
		cmp	m
		jnz	test11
		inx h
		mvi a	'i'
		cmp	m
		jnz	test11
		inx h
		mvi a	'm'
		cmp	m
		jnz	test11
		inx h
		mvi a	'e'
		cmp	m
		jnz	test11
		jmp	showtime	
test11
fail		mvi a	1
		ret			; return 1 as failed to find a command



;--------------------------------
;----------- Showhelp -----------
;--------------------------------

showhelp	; show help summary
		lxi h	HELPMSG
		call	print
		mvi a	0x00		; successful command run
		ret
		;
HELPMSG	DB CR DB LF DB "OS/80 Command Summary" DB  CR DB  LF
	DB "halt, load, save, run, dir, del, " DB CR DB LF 
	DB "mon, print, help" DB CR DB LF DB EOL


;---------------------------
;-------- PrintFile --------
;---------------------------

printFile	; send ascii text buffer to serial printer
		; assumptions: start of any text file is in BUFSTART
		; end of any text file will \0 char (NUL) in buffer
		; printer address is PRINTER
		;
		; (put any printer init stuff here)
		;
		lxi b	BUFSTART
		ldax b			; get MSB of BUFSTART
		mov h,a			; save in H
		inx b
		ldax b			; get LSB
		mov l,a			; save in L
		;
pfloop		mov a,m			; get char from mem
		cpi	NUL		; test for \0 - end of message
		jz pfend
		out	PRINTER
		inx h
		;mvi a	'.'		; (debugging stuff)
		;out	CONOUT
		;
		jmp	pfloop		; continue

pfend		; finished w/file
		ret			; done



;---------- Showtime ----------
showtime	; display current rtc time on console (use settime to initially 
		; set the clock, or 'time' will just show time since powerup).

		mvi a	CR
		out	CONOUT
		mvi a	LF
		out	CONOUT

		in	0x1d		; hours
		call	printdecimal

		mvi a	':'
		out	CONOUT

		in	0x1c		; minutes
		call	printdecimal

		mvi a	':'
		out	CONOUT

		in	0x1b		; seconds
		call	printdecimal
		
		mvi a	0		; return code
		ret


;-------------------------------
;----------- Loadfile ----------
;-------------------------------

loadfile	; load a file from disk, save in RAM
		; file loading starts at 0x0000 or entered address. As soon 
		; as the load is complete, return to the command prompt
		; NOTE: if loading a file so large it overwrites the stack pointer, 
		; the SP will be unknown and the return address will be corrupted.
		; This happens when loading large files.
			
		; set default locations for start to 0x0000
		lxi b	BUFSTART		; point to start location for loading
		mvi a	0x00
		stax b				; save MSB
		inx b
		stax b				; save LSB
		;		

		; read in filename, store it
		call	getFilename
		cpi	0x01		; test if error
		rz			; return on error

		
		; give the user the option to assign a start address
		; show the initial address, input either a CR to use the displayed address
		; or 4 hex chars and a CR for a new address

		lxi h	load_msg1	; show 1st part of message
		call	print
		; show current address
		lxi h	BUFSTART
		mov a,m
		call	printascii
		inx h
		mov a,m
		call	printascii
		lxi h	load_msg2	; and rest of message
		call	print

		; get CR or new value from conin
		call	getline
		lxi h	LINEIN		; see what was entered
		mov a,m
		cpi	LF
		jz	lf1		; enterted LF - use existing
		cpi	CR
		jz	lf1		; entered CR - use existing

		; hl points to 1st digit of address
		call	getAddress
		cpi	0x01
		rz			; return on error

		; start address in BUFTEMP
		lxi h	BUFTEMP
		lxi b	BUFSTART
		mov a,m
		stax b
		inx h
		inx b
		mov a,m
		stax b			; start address now in BUFSTART 
				
lf1		; OK - load the file
		mvi a 0x03			; disk load command
		out	DISK			; call routine
		;
		mvi a	0x00			; OK return code
		ret				; done


load_msg1	DB CR  DB LF  DB "Enter 4 digit Start address (" DB EOL
load_msg2	DB "H) or <enter>: " DB EOL




;------------------------------
;---------- Savefile ----------
;------------------------------

savefile	; save RAM to disk file
		; filename is after the 'save' command
		; set default addresses 

		lxi b	BUFSTART
		mvi a	0x00			; buffer start = 0x0000
		stax b
		inx b					
		stax b				; buffer start:lo address

		; end of buffer address 0x0100
		lxi b	BUFEND
		mvi a	0x01			; buffer end:high address
		stax b
		dcr a
		inx b
		stax b				; buffer end:lo address
		
		; get the filename to use
		call	getFilename
		cpi	0x01		; test if error
		rz			; return on error
		
		; give the user the option to assign a start address
		; show the initial address, input either a CR to use the displayed address
		; or 4 hex chars and a CR for a new address

		lxi h	save_msg1	; show 1st part of message
		call	print
		; show current address
		lxi h	BUFSTART
		mov a,m
		call	printascii
		inx h
		mov a,m
		call	printascii
		lxi h	save_msg2	; and rest of message
		call	print

		; get CR or new value from conin
		call	getline
		lxi h	LINEIN		; see what was entered
		mov a,m
		cpi	LF
		jz	sf1		; enterted LF - use existing
		cpi	CR
		jz	sf1		; entered CR - use existing

		; hl points to 1st digit of address
		call	getAddress
		cpi	0x01
		rz			; return on error

		; start address in BUFTEMP
		lxi h	BUFTEMP
		lxi b	BUFSTART
		mov a,m
		stax b
		inx h
		inx b
		mov a,m
		stax b			; start address now in BUFSTART 
		;

		; give the user the option to assign an ending address
		; show the initial address, input either a CR to use the displayed address
		; or 4 hex chars and a CR for a new address

sf1		lxi h	save_msg3	; show 1st part of message
		call	print
		; show current address
		lxi h	BUFEND
		mov a,m
		call	printascii
		inx h
		mov a,m
		call	printascii
		lxi h	save_msg4	; and rest of message
		call	print

		; get CR or new value from conin
		call	getline
		lxi h	LINEIN		; see what was entered
		mov a,m
		cpi	LF
		jz	sf2		; enterted LF - use existing
		cpi	CR
		jz	sf2		; entered CR - use existing

		; hl points to 1st digit of address
		call	getAddress
		cpi	0x01
		rz			; return on error

		; start address in BUFTEMP
		lxi h	BUFTEMP
		lxi b	BUFEND
		mov a,m
		stax b
		inx h
		inx b
		mov a,m
		stax b			; start address now in BUFEND 

		
sf2		mvi a 0x02		; disk save command
		out	DISK		; call routine


		mvi a	0x00		; error code for return
		ret

save_msg1	DB CR  DB LF  DB "Enter 4 digit Start address (" DB EOL
save_msg2	DB "H) or <enter>: " DB EOL
save_msg3	DB CR  DB LF  DB "Enter 4 digit End address (" DB EOL
save_msg4	DB "H) or <enter>: " DB EOL


;------------------------------
;----------- Dir --------------
;------------------------------

showDir		; show disk directory
		mvi a	CR
		out	CONOUT
		mvi a	LF
		out	CONOUT
		mvi a	0x01		;; command to show directory
		out	DISK			;; call disk sub-system

		mvi a	0x00		; return code
		ret


;-------------------------------
;------------ Delete -----------
;-------------------------------

delete		; delete a file on the SD card
		call	getFilename		; load the filename into a buffer
		cpi	0x01			; look for fail code=01
		rz				; filename load failed
		;
		mvi a	0x04			; command for delete
		out	DISK
		;
		mvi a	0x00			; return code
		ret
		
;------------------------------
;----------- Mon --------------
;------------------------------

;	Commands:	
;	x 	- exit monitor, return to OS/80
;	<cr>, n	- display next byte
;	b	- display previous byte
;	maaaa	- change memory address to aaaa (hex)
;	.nn	- change current memory byte to nn (hex)
;	daaaa	- display a page of memory starting at aaaa (hex)
;	gaaaa	- go to address aaaa (hex) and start execution from there 

mon		; machine language monitor
		; show banner message
		lxi h	mon_banner_msg
		call	print

		mvi a	0
		mov h,a			; set initial working address
		mov l,a	

mon1		;
		mvi a	CR
		out	CONOUT
		mvi a	LF
		out	CONOUT
		call	mon_showAddress	; show address of HL (memory) pointer
		mov a,m			; show memory contents
		call	printascii
		mvi a	' '		; space btwn data & command
		out	CONOUT
		
mon1a		call	mon_getByte	; get char in A
		cpi	'x'		; exit back to OS/80
		jnz	mon2
		lxi h	mon_exit_msg
		call	print
		mvi a	0
		ret			; return w/0 exit code
			
mon2		cpi	CR		; show next address
		jnz	mon3
		inx h
		jmp	mon1

mon3		cpi	'n'		; show next address
		jnz	mon4
		inx h
		jmp	mon1

mon4		cpi	'b'		; show previous address
		jnz	mon5
		dcx	h
		jmp	mon1

mon5		cpi	'.'		; enter byte at current address
		jnz	mon6				
		; get 2 ascii bytes, convert to binary, save in (HL)
mon5a		call	mon_getByte
		mov b,a
		call	mon_getByte
		mov c,a
		call	a2b		; convert to binary in A
		mov m,a			; save to memory
		inx h			; next address
		jmp	mon1
		

mon6		cpi	'm'		; change memory pointer: mf800
		jnz	mon7		
		call	mon_getByte
		mov	b,a
		call	mon_getByte
		mov c,a
		call	a2b		; get 2 bytes, store in H
		mov h,a
		;
		call	mon_getByte
		mov	b,a
		call	mon_getByte
		mov c,a
		call	a2b		; get 2 bytes, store in L
		mov l,a
		jmp	mon1		; show new address, byte and continue

mon7		cpi	'g'		; GOTO run a program: gf800
		jnz	mon8
		call	mon_getByte
		mov b,a
		call	mon_getByte
		mov c,a
		call	a2b
		mov h,a			; gets high order address
		;
		call	mon_getByte
		mov	b,a
		call	mon_getByte
		mov c,a
		call	a2b		; get 2 bytes, store in L
		mov l,a			
		;
		pchl			; start execution

mon8		cpi	'd'		; display a page of memory d1200
		jnz	mon9
		push h			; save current hl
		call	mon_getByte
		mov b,a
		call	mon_getByte
		mov c,a
		call	a2b
		mov h,a			; gets high order address
		;
		call	mon_getByte
		mov	b,a
		call	mon_getByte
		mov c,a
		call	a2b		; get 2 bytes, store in L
		mov l,a	
		;
		mvi a	CR
		out	CONOUT
		mvi a	LF
		out	CONOUT
		;		
		;	now show a single page from hl
		mvi d	16		; 16 lines of 16
mon8a		mvi c	16
mon8b		call	mon_showAddress	; show address
mon8c		mvi a	' '
		out	CONOUT
		mov a,m			; get byte
		call	printascii	; showit
		inx h			; next address
		dcr	c
		mov a,c
		cpi	0
		jnz	mon8c		; show 16 bytes
		
		mvi a	CR
		out	CONOUT
		mvi a	LF
		out	CONOUT
		dcr d
		mov a,d
		cpi 0
		jnz	mon8a		; show another line
		pop	h		; restore HL
		jmp	mon1		; back to command loop

mon9
mon_err		jmp	mon1a		


mon_getByte	; get an ascii char, return value in A
		in	CONIN_STAT
		cpi	0
		jz	mon_getByte
		;
		in	CONIN		; get char
		out	CONOUT		; echo char	
		ret


mon_showAddress		; display the address in HL
		mov a,h
		call	printascii
		mov a,l
		call	printascii
		mvi a	' '
		out	CONOUT
		ret

mon_banner_msg	DB CR DB LF DB "OS/80 Resident ML Monitor" DB CR DB LF DB EOL
mon_exit_msg	DB CR DB LF DB "Leaving ML Monitor" DB CR DB LF DB EOL

;-------------------------------
;-------- End of Code ----------
;-------------------------------


