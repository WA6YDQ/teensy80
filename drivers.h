/* drivers.h - this is the BIOS for the 80sim.ino
 *
 * NOTE: the Altair has Serial I/O at:
 *  0x10 (status port 0, console status, input only, return count of avail chars)
 *  0x11 (data port 0, console device in/out (byte at a time)
 * Altair disk units at:
 *  0x08 (disk device octal 10, output - Select/enable controller & drive)
 *  0x08 (disk device octal 10, input  - Return status of drive and controller)
 *  0x09 (disk device octal 11, output - Control disk function)
 *  0x09 (disk device octal 11, input  - Indicates current sector position of disk)
 *  0x0a (disk device octal 12, output - Write data to disk)
 *  0x0a (disk device octal 12, input  - Read data from disk)
 *  
 *  
 * Rest of I/O ports:
 *  0x (status port 1, input tape reader, output punch device (paper tape))
 *  0x (data port 1, input tape reader, output punch device (paper tape))
 *  0x12 (input port, LINE32)
 *  0x13 (input port, LINE31)
 *  0x14 (output port, LINE33)
 *  0x15 (output port, LINE34)
 *  0x16 (output port, TIMER calls 1-255 msec timer delay, blocking)
 *  0x17 (output port, TIMER calls 10-2.55 seconds timer delay, blocking)
 *  0x18 (output port, TONE enables audio tone, 10-2550hz, on TONEPIN)
 *  0x19 (output port, TONE disables tone on TONEPIN);
 *  0x1a (output port, BEEP (1kc for 250msec duration)
 *  0x1b (output, set time from memory locations)
 *  0x1b (input, time() - return seconds 
 *  0x1c (input, time() - return minutes
 *  0x1d (input, time() - return hours
 *  0x1f (output port, serial output (printer)
 *  0xfe (output port, bank selection (0-6)
 *  
 * 
 *  
 *  (see https://github.com/simh/simh/blob/master/ALTAIR/altair_dsk.c for full specs)
 *  
 */

extern uint8_t *RAM;
extern uint8_t BANK;

/* show files on the SD card */
void printDir(File dir, int numTabs) {
   while(true) {
     
     File entry =  dir.openNextFile();
     if (! entry) {
       // no more files
       break;
     }
     for (uint8_t i=0; i<numTabs; i++) {
       Serial.print('\t');
     }
     Serial.print(entry.name());
     if (entry.isDirectory()) {
       Serial.println("/");
       printDir(entry, numTabs+1);
     } else {
       // files have sizes, directories do not
       Serial.print("\t\t");
       Serial.println(entry.size(), DEC);
     }
     entry.close();
   }
   return;
}


/* input routines */
uint8_t input(uint8_t addr) {

    // Altair serial console STATUS port - return 0 or number of chars available
    if (addr == 0x10) {     
        return(Serial.available());
    }

    // Altair serial console INPUT port - return char each time called
    if (addr == 0x11) {     
        return(Serial.read()); 
    }

    // input 1 - switched line. When contacts closed, line is HIGH
    if (addr == 0x12) {
        return digitalRead(LINE32);
    }

    // input 2 - switched line. When contacts close, line is HIGH
    if (addr == 0x13) {
        return digitalRead(LINE31);
    }

    // time() SECONDS - return time in seconds
    if (addr == 0x1b) {
        time_t t=now();
        return second(t);
    }

    // time() MINUTES - return time in minutes
    if (addr == 0x1c) {
        time_t t=now();
        return minute(t);
    }

    // time HOURS - return time in hours
    if (addr == 0x1d) {
        time_t t=now();
        return hour(t);
    }
    
    return 0;
}




/* output routines: send a value to a specified address */
void output(uint8_t val, uint8_t addr) {



    if (addr == 0 || addr == 1 || addr == 0x11) {       // serial output to console device
        Serial.write(val);
        return;
    }

    if (addr == 0x1f) {     // serial printer
        Printer.write(val);
        return;
    }


    // output line 1 - open collector xstr. If out is HIGH, transistor is closed
    if (addr == 0x14) {
        if (val == 0) 
            digitalWrite(LINE33,0);
        else
            digitalWrite(LINE33,1);
        return;
    }

    // output line 2 - open collector xstr. If out is HIGH, transistor is closed
    if (addr == 0x15) {
        if (val == 0) 
            digitalWrite(LINE34,0);
        else
            digitalWrite(LINE34,1);
        return;
    }


    // output port delay timer: blocking delay of (val) msec
    if (addr == 0x16) {
        delay(val);
        return;
    }

    // output port delay timer: blocking delay of (val * 10) msec
    if (addr == 0x17) {
        delay(val*10);
        return;
    }


    // tone output enable - val*10 gives 10hz thru 2.550 khz address 0x18
    if (addr == 0x18) {
        tone(TONEPIN, val * 10);
        return;
    }

    // tone OFF - address 0x19
    if (addr == 0x19) {
        noTone(TONEPIN);
        return;
    }

    // BEEP only (250 msec tone at 1000hz
    if (addr == 0x1a) {
        tone(TONEPIN,1000,250);
        return;
    }

    // set real time clock on teensy 
    if (addr == 0x1b) {     
        // HMSDMY
        unsigned int HH, MM, SS;
        
        char TEMP[30];
        HH = (RAM[0xeff9]*10) + RAM[0xeff8]; 
        MM = (RAM[0xeff7]*10) + RAM[0xeff6];
        SS = (RAM[0xeff5]*10) + RAM[0xeff4];
        
        Console.print("\r\ntime set to "); 
        sprintf(TEMP,"%d-%d-%d\n\r",HH,MM,SS);
        Console.print(TEMP);

        // set the time (but not the date (yet))
        setTime(HH,MM,SS,1,1,2021);

        memset(TEMP,0,sizeof(TEMP));
        //time_t t=now();
        sprintf(TEMP,"%d:%d:%d\r\n",hour(),minute(),second());
        Console.print("\r\ntime is ");
        Console.print(TEMP);
       
        return;
    }

    /* memory bank selection based on sending a byte (0-6) to output port 0xfe */
    if (addr == 0xfe) {             // select 32K memory bank 0-6
        if (val < 7) BANK = val;
        return;
    }



    /* disk emulation routines
     * since we lack dedicated hardware for a disk drive interface, we just use hi-level
     * routines for disk operations.
    */

    /* the following addresses are defined in the OS/80 source code (os80.asm)*/
    #define FILENAMEADDR 0xef4c     // RAM address of filename begin (12 bytes max in 8.3 format)
    #define BUFSTART 0xeffe         // RAM address of start of buffer in HI:LO format
    #define BUFEND 0xeffc           // RAM address of end of buffer in HI:LO format
    
    if (addr == 0x20) {
        
        if (val == 1) {     // show directory on SD card
            File root;
            root = SD.open("/");
            printDir(root,0);
            root.close();
            Serial.flush();
            return;
        }
        
        if (val == 2) {     // save from RAM to SD card
            char filename[15];
            uint16_t cnt=0, start_addr = (RAM[BUFSTART] * 256) + RAM[BUFSTART+1]; 
            uint16_t end_addr = (RAM[BUFEND] * 256) + RAM[BUFEND+1];
            if ((end_addr - start_addr) < 1) {
                Console.print("\r\nStart address >= End address"); Console.flush();
                return;
            }
            
            Console.print("\r\nStart Address: "); Console.print(start_addr, HEX); 
            Console.print("\r\nEnd Address: "); Console.print(end_addr, HEX);
            Console.print("\r\n"); Console.flush();
            
            for (cnt=0; cnt<15; cnt++) filename[cnt] = RAM[FILENAMEADDR+cnt];
            Console.print("\r\nSaving to file "); Console.print(filename); Console.print("\r\n"); Console.flush();
            //return;
            
            File sdfile;
            sdfile = SD.open(filename,FILE_WRITE);
            if (!sdfile) {      // error creating/opening
                Console.print("\r\nError creating/opening file "); Console.print(filename); Console.print("\r\n"); Console.flush();
                return;
            }
            for (cnt=start_addr; cnt<end_addr; cnt++) sdfile.write(RAM[cnt]);
            sdfile.flush();
            sdfile.close();
            Console.print("\r\nFile "); Console.print(filename); Console.print(" saved\r\n"); Console.flush();
            return;
        }
        
        if (val == 3) {     // load file from SD card, store in RAM
            char filename[15];
            uint16_t cnt=0, start_addr = (RAM[BUFSTART]*256) + RAM[BUFSTART+1];
            if (start_addr < 0 || start_addr > 65535) {
                Console.print("\r\nBad Start address: "); Console.print(start_addr, HEX); Console.print("\r\n"); Console.flush();
            } else {
                Console.print("\r\nStart Address: "); Console.print(start_addr, HEX);
            }
            for (cnt=0; cnt<15; cnt++) filename[cnt] = RAM[FILENAMEADDR+cnt];

            File sdfile;
            sdfile = SD.open(filename);
            if (!sdfile) {  // error opening file
                Console.print("\r\nFile "); Console.print(filename); Console.print(" not found\r\n"); Console.flush();
                return;
            }
            Console.print("\r\nLoading file "); Console.print(filename); Console.print("\r\n"); Console.flush();
            while (sdfile.available()) {
                RAM[start_addr++] = sdfile.read();
            }
            Console.print("\r\nFile "); Console.print(filename); Console.print(" loaded\r\n"); Console.flush();
            return;
        }

        if (val == 4) {     // delete a file from the SD card
            char filename[15];
            uint16_t cnt = 0;
            File sdfile;
            
            for (cnt=0; cnt<15; cnt++) filename[cnt] = RAM[FILENAMEADDR+cnt];
            //Console.print("\r\nDeleting "); Console.print(filename); Console.print("\r\n"); Console.flush();
            
            /* good filename? */
            sdfile = SD.open(filename);
            if (!sdfile) {
                Console.print("\r\n"); Console.print(filename); Console.print(" not found"); Console.flush();
                return;
            }
            sdfile.close();
            SD.remove(filename);
            Console.print("\r\n"); Console.print(filename); Console.print(" deleted"); Console.flush();
            /* done */
            return;        
        }
        
    }






    //------ unknown device selected if we get here -----
    return;
}
