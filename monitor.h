/*  This is a monitor program used to run outside the 8080 simulator.
 *  It is started when a HLT (0x76) instruction is encountered, or the ABORT button is 
 *  pressed while the simulator is running. Use the 'cont' command to resume execution.
 *  Typing 'help' displays a list of commands.
 *  
 *  Formatted output is written to be displayed on an 80x24 display console.
 *  
 *  (C) K Theis 10/2021
 */

#define MAXROW  22          // max number of rows to display before pausing display


#include <string.h>
#include <SD.h>
#include <SPI.h>

/* show files on the SD card */
void printDirectory(File dir, int numTabs) {
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
       printDirectory(entry, numTabs+1);
     } else {
       // files have sizes, directories do not
       Serial.print("\t\t");
       Serial.println(entry.size(), DEC);
     }
     entry.close();
   }
   return;
}


void memtest() {
    /* run a non-destructive memory test */
    uint8_t oldVal;
    extern uint8_t *RAM;
    char temp[40];
    Serial.println("\r\nRunning non-destructive memory test");
    for (int n=0; n<RAMSIZE; n++) {
        oldVal = RAM[n];
        RAM[n] = 0x00;
        if (RAM[n] != 0x00) sprintf(temp,"Memory Error at address %04Xh\r\n",n);
        RAM[n] = 0xaa;
        if (RAM[n] != 0xaa) sprintf(temp,"Memory Error at address %04Xh\r\n",n);
        RAM[n] = 0x55;
        if (RAM[n] != 0x55) sprintf(temp,"Memory Error at address %04Xh\r\n",n);
        RAM[n] = 0xff;
        if (RAM[n] != 0xff) sprintf(temp,"Memory Error at address %04Xh\r\n",n);
        RAM[n] = oldVal;
        if (RAM[n] != oldVal) sprintf(temp,"Memory Error at address %04Xh\r\n",n);
    }
    Serial.println("Memory Test Complete"); Serial.flush();
}

void loadfile() {       // load a binary file from SD card to main memory
    char filename[20];
    char temp[120];
    char ch;
    char startaddr[20];
    File sdfile;
    extern uint16_t PC;
    extern uint8_t *RAM;
    int cnt=0;
    
    
    /* get filename to load */
    cnt = 0;
    Serial.print("Filename? "); Serial.flush();
    memset(filename,0,sizeof(filename));
    while (1) {
        if (Serial.available())
            ch = Serial.read();
        else
            continue;
        Serial.print(ch); Serial.flush();
        if (ch == '\n' || ch == '\r') break;
        filename[cnt++] = ch;
    }
    filename[cnt]='\0';
    Serial.println(); Serial.flush();
    
    /* good filename? */
    sdfile = SD.open(filename);
    if (!sdfile) {
        Serial.print(filename); Serial.print(" not found\r\n"); Serial.flush();
        return;
    }
    sdfile.close();
    
    
    /* get start address */
    memset(startaddr,0,sizeof(startaddr));
    ch = '\0';
    Serial.print("Start Address? ");
    cnt = 0;
    memset(startaddr,0,sizeof(startaddr));
    while (1) {
        if (Serial.available())
            ch = Serial.read();
        else
            continue;
        Serial.print(ch); Serial.flush();
        if (ch == '\n' || ch == '\r') break;
        startaddr[cnt++] = ch;
    }
    startaddr[cnt] = '\0';
    Serial.println(); Serial.flush();
    
    /* load file */
    Serial.print("Searching for ");
    Serial.println(filename);
    Serial.flush();
    sdfile = SD.open(filename);
    if (sdfile) {
        Serial.print(filename);
        Serial.println(" opened");
        Serial.flush();
        /* read from the file, save to memory */
        PC = atoi(startaddr);
        cnt = 0;
        sprintf(temp,"%04X  ",PC);
        Serial.print(temp);         // show address
        while (sdfile.available()) {
            RAM[PC] = sdfile.read();
            sprintf(temp,"%02X ",RAM[PC]);
            Serial.print(temp);
            cnt++;
            if (cnt == 16) {
                cnt = 0;
                Serial.println();
                sprintf(temp,"%04X  ",PC+1);
                Serial.print(temp);     // show address
            }
            PC++;
        }
        Serial.print("\r\n\nLoaded "); Serial.print(PC-1, DEC); Serial.println(" bytes into RAM");
        sdfile.close();
    } else {
        Serial.print(filename);
        Serial.println(" failed to load.");
        Serial.flush();
    }

    /* done */
    Serial.flush();
    return;
    
}

void savefile() {       // save RAM contents to SD file
    return;
}


void slist() {      /* display text file onto the console */
    File sdfile;
    int cnt;
    char filename[20];
    char ch;
    int FLAG = 0;
    
    /* get filename to load */
    cnt = 0;
    Serial.print("Filename? "); Serial.flush();
    memset(filename,0,sizeof(filename));
    while (1) {
        if (Serial.available())
            ch = Serial.read();
        else
            continue;
        Serial.print(ch); Serial.flush();
        if (ch == '\n' || ch == '\r') break;
        filename[cnt++] = ch;
    }
    filename[cnt]='\0';
    Serial.println(); Serial.flush();
    
    /* good filename? */
    sdfile = SD.open(filename);
    if (!sdfile) {
        Serial.print(filename); Serial.print(" not found\r\n"); Serial.flush();
        return;
    }
    /* file open - now display it */
    while (sdfile.available()) {
        FLAG = 0;
        ch = sdfile.read();
        Serial.write(ch);
        if (ch == '\n' || ch == '\0') { 
            Serial.print("\r");
            FLAG = 1;
        }
        if (ch == '\r') {
            Serial.print("\n");
            FLAG = 1;
        }
        if (FLAG)   
          cnt++;
        if (cnt > 24) {     /* page break */
             Serial.print("\r\nq to quit, any key to continue: ");
             while (!Serial.available()) ;
             ch = Serial.read();
             Serial.print("\r\n");
             if (toupper(ch) == 'Q') return;
             cnt = 0;
       }
    }
        
    sdfile.close();
    Serial.print("\r\n");
    return;
}



void dumpfile() {      /* display hex dump of a file onto the console */
    File sdfile;
    int cnt;
    char filename[20];
    char ch;
    char temp[40];
    uint16_t addr;
    
    /* get filename to load */
    cnt = 0;
    Serial.print("Filename? "); Serial.flush();
    memset(filename,0,sizeof(filename));
    while (1) {
        if (Serial.available())
            ch = Serial.read();
        else
            continue;
        Serial.print(ch); Serial.flush();
        if (ch == '\n' || ch == '\r') break;
        filename[cnt++] = ch;
    }
    filename[cnt]='\0';
    Serial.println(); Serial.flush();
    
    /* good filename? */
    sdfile = SD.open(filename);
    if (!sdfile) {
        Serial.print(filename); Serial.print(" not found\r\n"); Serial.flush();
        return;
    }
    /* file open - now display it */
    cnt = 0; addr = 0;
    sprintf(temp,"%04X  ",addr);
    Serial.print(temp);
    while (sdfile.available()) {
        ch = sdfile.read();
        sprintf(temp,"%02X ",ch);
        Serial.print(temp);
        cnt++; addr++;
        if (cnt < 16) continue;
        Serial.print("\r\n");
        sprintf(temp,"%04X  ",addr);
        Serial.print(temp);
        cnt = 0;
        
    }
    sdfile.close();
    Serial.print("\r\n");
    return;
}


void deleteFile() {     /* delete a file from the sd card */
    int cnt = 0;
    char filename[40];
    char ch;
    File sdfile;
    
    /* get filename to load */
    cnt = 0;
    Serial.print("Filename? "); Serial.flush();
    memset(filename,0,sizeof(filename));
    while (1) {
        if (Serial.available())
            ch = Serial.read();
        else
            continue;
        Serial.print(ch); Serial.flush();
        if (ch == '\n' || ch == '\r') break;
        filename[cnt++] = ch;
    }
    filename[cnt]='\0';
    Serial.println(); Serial.flush();
    
    /* good filename? */
    sdfile = SD.open(filename);
    if (!sdfile) {
        Serial.print(filename); Serial.print(" not found\r\n"); Serial.flush();
        return;
    }
    sdfile.close();
    SD.remove(filename);
    Serial.print("\r\n"); Serial.print(filename); Serial.print(" deleted\r\n"); Serial.flush();
    return;
}

void createFile() {     /* create an empty file & write text to it */
    int cnt = 0;
    char filename[40];
    char ch;
    char linein[80];
    File sdfile;

    /* get filename to load */
    cnt = 0;
    Serial.print("Filename? "); Serial.flush();
    memset(filename,0,sizeof(filename));
    while (1) {
        if (Serial.available())
            ch = Serial.read();
        else
            continue;
        Serial.print(ch); Serial.flush();
        if (ch == '\n' || ch == '\r') break;
        filename[cnt++] = ch;
    }
    filename[cnt]='\0';
    Serial.println(); Serial.flush();

    /* create a blank file every time */
    sdfile = SD.open(filename,FILE_WRITE);

    if (!sdfile) {      // error creating/opening
        Serial.print("\r\nError creating/opening file "); Serial.print(filename); Serial.print("\r\n"); Serial.flush();
        return;
    }

    /* file opened/created */
    Serial.print("type '.exit' to stop editing and save file\r\n");
    
getloop:
    memset(linein,0,sizeof(linein));
    while (!Serial.available()) ;
    cnt = 0;
    while (1) {
        if (Serial.available()) 
            ch = Serial.read();
        else
            continue;
        Serial.print(ch); Serial.flush();
        linein[cnt++] = ch;
        if (ch == '\r' || ch == '\n') break;
    }
    /* got a line of text - write to disk */
    if (strncmp(linein,".exit",5) != 0) { 
        sdfile.write(linein);
        Serial.print("\r\n");
        goto getloop;
    }
    /* done */
    
    sdfile.flush();
    sdfile.close();
    Serial.print("\r\n"); Serial.flush();
    return;
    
}


/* ML Monitor */
int mon() {
    char linein[40], ch, temp[40];
    uint16_t addr, daddr; 
    uint8_t val3, val2, val1, val0;
    extern uint8_t *RAM;
    extern uint16_t PC;
    uint8_t cnt = 0;
    addr = 0x00; 
    
    Serial.println("\r\n--- ML Monitor ---"); 
    Serial.println("x       exit to main menu");
    Serial.println("n/<CR>  next address");
    Serial.println("b       previous address");
    Serial.println(".nn     load 2 digit (hex) to current memory");
    Serial.println("mnnnn   set current address (hex) to nnnn");
    Serial.println("gnnnn   GO to (hex) address nnnn and start execution");
    Serial.println("dnnnn   dump a page of memory starting at (hex) address nnnn");
    Serial.println(); Serial.flush();
    /* main loop */
     
    while (1) {
        /* show address & value */
        cnt = 0; memset(linein,0,sizeof(linein)); memset(temp,0,sizeof(temp));
        sprintf(temp,"\r\n%04X %02X ",addr,RAM[addr]); Serial.print(temp); Serial.flush();
        while (1) {
            if (Serial.available())
                ch = Serial.read();
            else
                continue;
            Serial.print(ch); Serial.flush();
            linein[cnt++] = ch;
            if (ch == '\n' || ch == '\r') break;
        }
        linein[cnt] = '\0';
        linein[0] = toupper(linein[0]);
        /* pre-set addresses */
        val3 = (toupper(linein[1])) <= '9' ? (toupper(linein[1]))-'0' : (toupper(linein[1]))-'A'+10;
        val2 = (toupper(linein[2])) <= '9' ? (toupper(linein[2]))-'0' : (toupper(linein[2]))-'A'+10;
        val1 = (toupper(linein[3])) <= '9' ? (toupper(linein[3]))-'0' : (toupper(linein[3]))-'A'+10;
        val0 = (toupper(linein[4])) <= '9' ? (toupper(linein[4]))-'0' : (toupper(linein[4]))-'A'+10;
            
        /* test 1st char of line */
        
        if (linein[0] == '.') {     // add byte value to memory
            RAM[addr++] = (16*val3)+val2;
            continue;
        }
        
        if (linein[0] == 'M') {     // (memory) get address, change to it
            addr = (val3*4096) + (val2*256) + (val1*16) + val0;
            continue;
        }
        
        if (linein[0] == 'X') return 0;
        
        if (linein[0] == 'N' || linein[0] == '\n' || linein[0] == '\r') {     // next address
            addr++;  
            continue;
        }
        
        if (linein[0] == 'B') {     // (back) previous address
            addr--; 
            continue;
        }

        if (linein[0] == 'D') {     // dump a page of memory
            daddr = (val3*4096) + (val2*256) + (val1*16) + val0;
            int cnt = 0;
            sprintf(temp,"%04X  ",daddr);
            Serial.print(temp);
            for (int a=0; a<256; a++) {
                sprintf(temp,"%02X ",RAM[daddr+a]);
                Serial.print(temp);
                cnt++;
                if (cnt < 16) continue;
                cnt = 0;
                sprintf(temp,"\r\n%04X  ",(uint16_t)daddr+a+1);
                Serial.print(temp);
            }
            continue;
        }

        if (linein[0] == 'G') {     // GOTO address nnnn and run the program
            Serial.println("\r\n\n");
            PC = (val3*4096) + (val2*256) + (val1*16) + val0;
            return 1;
        }
        
        // bad char
        Serial.println("\r\nEh?"); Serial.flush();
        continue;
    }
}


void diss() {       /* dissassembler */

    #include "decoder.h"
    uint16_t addr, valH;
    uint8_t  val, val0, val1, val2, val3;
    extern uint8_t *RAM;
    char temp[40], startaddr[20];
    int linecount = 0;
    int cnt = 0;
    char ch;

getnum:
    /* get start address in hex, 4 digits only */
    memset(startaddr,0,sizeof(startaddr));
    ch = '\0';
    Serial.print("Start Address? (ie: F2C0): ");
    cnt = 0;
    memset(startaddr,0,sizeof(startaddr));
    while (1) {
        if (Serial.available())
            ch = Serial.read();
        else
            continue;
        Serial.print(ch); Serial.flush();
        if (ch == '\n' || ch == '\r') break;
        startaddr[cnt++] = ch;
    }
    startaddr[cnt] = '\0';
    if (cnt != 4) {
        Serial.print("\r\n4 digit hex numbers only (ie: F2C0)\r\n");
        goto getnum;
    }
    val3 = (toupper(startaddr[0])) <= '9' ? (toupper(startaddr[0]))-'0' : (toupper(startaddr[0]))-'A'+10;
    val2 = (toupper(startaddr[1])) <= '9' ? (toupper(startaddr[1]))-'0' : (toupper(startaddr[1]))-'A'+10;
    val1 = (toupper(startaddr[2])) <= '9' ? (toupper(startaddr[2]))-'0' : (toupper(startaddr[2]))-'A'+10;
    val0 = (toupper(startaddr[3])) <= '9' ? (toupper(startaddr[3]))-'0' : (toupper(startaddr[3]))-'A'+10;
    valH = (val3*4096) + (val2*256) + (val1*16) + val0;
    
    Serial.print("\r\n");
    
    for (addr = valH; addr < RAMSIZE; addr++) {
        memset(temp,0,sizeof(temp));
        val = RAM[addr];
        linecount++;
        if (linecount > MAXROW) {
            Serial.print("\r\nq to quit, any key to continue: ");
            while (!Serial.available()) ;
            ch = Serial.read();
            Serial.print("\r\n");
            if (toupper(ch) == 'Q') return;
            linecount = 0;
            
        }
        if (oplen[val] == 1) {      // single byte opcode
            sprintf(temp,"%04X  %s\r\n",addr,opcode[val]);
            Serial.print(temp);
        }
        if (oplen[val] == 2) {      // 2 byte opcode
            sprintf(temp, "%04X  %s\t%02XH\r\n",addr,opcode[val],RAM[addr+1]);
            Serial.print(temp);
            addr++;
        }
        if (oplen[val] == 3) {      // 3 byte opcode
            sprintf(temp, "%04X  %s\t%04XH\r\n",addr,opcode[val],RAM[addr+1] + (256* RAM[addr+2]));
            Serial.print(temp);
            addr += 2;
        }
        
    }
    Serial.print("\r\n\n");
    return;
}

void showHelp() {       /* show command summary */
    Serial.print("\r\n");
    Serial.print("COMMAND                   SUMMARY\r\n");
    Serial.print("===============================================================================\r\n");
    Serial.print("run       Set PC to 0, clear the flags then resume execution at point just\r\n");
    Serial.print("          after the stop that invoked this monitor.\r\n");
    Serial.print("reset     Set PC to 0, clear flags.\r\n");
    Serial.print("memtest   Non-destructivly test RAM memory.\r\n");
    Serial.print("dir       Show directory of files on the SD card.\r\n");
    Serial.print("dis       Dissassemble RAM memory showing 8080 opcodes. You will be prompted\r\n");
    Serial.print("          for the 4 digit start address in hex. At page breaks, press <cr> to\r\n");
    Serial.print("          continue or 'q' to abort the listing.\r\n");
    Serial.print("load      Load a binary (.com or .bin) file from SD card to RAM memory. \r\n");
    Serial.print("          Use 'cont' to execute the newly loaded program. You will be\r\n");
    Serial.print("          prompted for a filename and a 4 digit hex address.\r\n");
    Serial.print("save      Save the contents of RAM memory to a file on the SD card. You will\r\n");
    Serial.print("          be prompted for a file name, a 4 digit hex start address and a\r\n");
    Serial.print("          4 digit hex ending address.\r\n");
    Serial.print("slist     Display a text file contents on console port. You will be\r\n");
    Serial.print("          prompted for the filename.\r\n");
    Serial.print("dump      Display file contents in hex on console port. You will be\r\n");
    Serial.print("          prompted for the filename.\r\n");
    Serial.print("delete    Delete a file from the SD card. You will be prompted for a filename.\r\n");
    Serial.print("create    Create/overwrite a file, add lines of text to the new file.\r\n");
    Serial.print("mon       Enter a machine language monitor program to edit the RAM contents.\r\n");
    Serial.print("help      Show this help message.\r\n");
    Serial.print("\r\n");

    return;
}

void abortRun() {       /* abort button pressed or an 8080 HLT (0x76) instruction occured */

    char ctemp[40];   // hold printed value
    char linein[40];
    extern uint16_t    PC, A, BC, DE, HL, StackP;
    extern uint8_t     OP;
    extern bool        C, Z, P, AC, S, INTE;
    int cnt = 0;
    char ch;
    extern void reset();

    digitalWrite(RUNLED,0);
    digitalWrite(HALTLED,1);
    Serial.println("\r\n--- Monitor ---");

    /* show registers */
    sprintf(ctemp,"PC: %04Xh  OP Code: %02Xh\r\n",PC,OP);
    Serial.print(ctemp);
    sprintf(ctemp," A: %02Xh\r\n",A);
    Serial.print(ctemp);
    sprintf(ctemp,"BC: %04Xh\r\n",BC);
    Serial.print(ctemp);
    sprintf(ctemp,"DE: %04Xh\r\n",DE);
    Serial.print(ctemp);
    sprintf(ctemp,"HL: %04Xh\r\n",HL);
    Serial.print(ctemp);
    sprintf(ctemp,"SP: %04Xh\r\n",StackP);
    Serial.print(ctemp);

    Serial.println("\r\nType 'help' for a command list");    
    Serial.flush();
    
    while (1) {
        Serial.print("cmd>"); Serial.flush();
        memset(linein,0,sizeof(linein));
        cnt = 0;
        while (1) {
            if (Serial.available())
                ch = Serial.read();
            else
                continue;
            Serial.print(ch); Serial.flush();
            linein[cnt++] = ch;
            if (ch == '\n' || ch == '\r') break;
        }
        Serial.println(); Serial.flush();
        
        if (linein[0] == '\r' || linein[0] == '\n') continue;   // ignore <cr>
        
        if (strncmp(linein,"memtest",7)==0) {    // non-destructive main-memory test
            memtest();
            continue;
        }
        
        if (strncmp(linein,"dir",3)==0) {        // show directory of SD card
            File root;
            root = SD.open("/");
            printDirectory(root,0);
            root.close();
            Serial.flush();
            continue;
        }
        
        if (strncmp(linein,"run",3)==0) {       // reset then return to execution
            reset();
            return;
        }

        if (strncmp(linein,"help",4)==0) {       // show commands
            showHelp();
            Serial.flush();
            continue;
        }

        if (strncmp(linein,"load",4)==0) {       // load an SD file to RAM
            loadfile();
            continue;
        }

        if (strncmp(linein,"save",4)==0) {      // save RAM to SD file
            savefile();
            continue;
        }

        if (strncmp(linein,"slist",5)==0) {     // display text file
            slist();
            continue;
        }

        if (strncmp(linein,"dump",4)==0) {      // display hex dump of file
            dumpfile();
            continue;
        }

        if (strncmp(linein,"delete",6)==0) {    // delete a file from the sd card
            deleteFile();
            continue;
        }

        if (strncmp(linein,"create",6)==0) {    // create a text file on disk
            createFile();
            continue;
        }

        if (strncmp(linein,"reset",5)==0) {     // reset PC and regs
            reset();
            continue;
        }

        if (strncmp(linein,"mon",3)==0) {       // ml monitor
            if (mon()) return;                  // if mon() returns 1, return to simulator, else stay in abortRun()
            Serial.println(); Serial.flush();
            continue;
        }

        if (strncmp(linein,"dis",3)==0) {       // dissassembler
            diss();
            continue;
        }
        
        Serial.println("EH?"); Serial.flush();
        continue;
    }


    
    while(1);
    
}
