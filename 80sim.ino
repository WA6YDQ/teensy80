/*
 * 80sim.ino  - 8080 simulator for the teensy 4.1
 * (C) k theis <theis.kurt@gmail.com> 10/2021
 * 
 * 
 * 
 */

/* define the system */
// #define TESTCODE_LOCAL     // uncomment to pre-load a test routine
// #define TESTCODE_DISK      // uncomment to pre-load LOAD.COM from the SD card 
//#define BANKDEF              // use banked memory if un-commented

#define DEBUG 0
#define RAMSIZE 262144          // was 65536
#define DEBOUNCE 80             // msec delay for debouncing a switch
#define SERIAL1_PORT_SPEED 9600 // baud rate of db-9 serial port
#define Console Serial          // allow us to change to Serial1 (or whatever) later
#define Printer Serial2         // printer rs-232 port
#define PrinterBaudRate 300

/* for banked memory, re-define the PC */
#define PCB PC>32768 ? PC : (((32768*BANK)+PC) & 0x3fff)

// hardware (lights, switches)
#define ABORTPIN 2      // momentary switch input for INT switch
#define RESETPIN 3      // momentary switch input for RESET switch
#define LOADPIN  4      // Pressing starts file transfer from remote device
#define INTPIN   9      // Interrupt pin for 8080 (depends on INTE)

#define RUNLED  5      // LED output (active 1) when disk activity happening
#define HALTLED 6      // LED output (active 1) when any IO happening
#define ATTNLED 30     // LED output (active 1) when some routine needs to alert the user

#define LINE31 31       // input line 0x13
#define LINE32 32       // input line 0x12
#define LINE33 33       // output line 0x14
#define LINE34 34       // output line 0x15

#define TONEPIN 35      // output tone, addr 0x18 (off is 0x19)


/* defines for SD card on teensy 4.1 */
#include <SD.h>
#include <SPI.h>
#include <TimeLib.h>
#include <DS1307RTC.h> 
const int chipSelect = BUILTIN_SDCARD;
File loadFile;

#include <string.h>
#include "drivers.h"        // specific hardware drivers for 80sim
#include "decoder.h"        // op code decoding
#include "monitor.h"        // abort monitor routines


/* setup PSRAM memory */
EXTMEM char PStest[80];


/* define the registers */
#ifdef BANKDEF 
uint32_t PC;
#endif
#ifndef BANKDEF 
uint16_t PC;
#endif

uint16_t    A, BC, DE, HL, StackP, temp, carry, hi, lo;
uint8_t     OP;
bool        C, Z, P, AC, S, INTE;

/* define 8 bit main memory */
uint8_t *RAM;
uint8_t BANK = 0;       // 0-7, value * 32768 added memory pointer, set by output port 0xfe=0-7
                        // use PC = (PC<32768) ? PC : (32768*BANK)+PC to determine RAM address
                        // banks only apply to upper part of memory (32768-65535). 0-32767 are unchanged.


/* gp variables */
int FLAG = 0;


/************ END OF SYSTEM DEFINES *************/

/* test for parity */
int parity(unsigned char ptest) {    
    int p=0;

    if (ptest==0)   /* odd parity/no parity */
        return(0);
    
    while (ptest != 0) {
        p ^= ptest;
        ptest >>= 1;
    }
    if ((p & 0x1)==0)   /* 0=even parity */
        return(1);  /* parity set */
    else
        return(0);  /* parity not set */
}


/* get value of register pair */
int getpush(int reg) {

    int stat;

    switch (reg) {
        case 0:
            return (BC);
        case 1:
            return (DE);
        case 2:
            return (HL);
        case 3:
            stat = A << 8;
            if (S) stat |= 0x80;
            if (Z) stat |= 0x40;
            if (AC) stat |= 0x10;
            if (P) stat |= 0x04;
            stat |= 0x02;
            if (C) stat |= 0x01;
            return (stat);
        default:
            break;
    }
    return 0;
}

/* put value of register pair */
void putpush(int reg, int data) {

    switch (reg) {
        case 0:
            BC = data;
            break;
        case 1:
            DE = data;
            break;
        case 2:
            HL = data;
            break;
        case 3:
            A = (data >> 8) & 0xff;
            S = Z = AC = P = C = 0;
            if (data & 0x80) S  = 1;
            if (data & 0x40) Z  = 1;
            if (data & 0x10) AC = 1;
            if (data & 0x04) P  = 1;
            if (data & 0x01) C  = 1;
            break;
        default:
            break;
    }
}

/* Test an 8080 flag condition and return 1 if true, 0 if false */
int cond(int con)
{
    switch (con) {
        case 0:
            if (Z == 0) return (1);
            break;
        case 1:
            if (Z != 0) return (1);
            break;
        case 2:
            if (C == 0) return (1);
            break;
        case 3:
            if (C != 0) return (1);
            break;
        case 4:
            if (P == 0) return (1);
            break;
        case 5:
            if (P != 0) return (1);
            break;
        case 6:
            if (S == 0) return (1);
            break;
        case 7:
            if (S != 0) return (1);
            break;
        default:
            break;
    }
    return (0);
}

/* Set flags based on val */
void setarith(int val) {
    if (val & 0x100)    // >= 256
        C = 1;
    else
        C = 0;
    if (val & 0x80) {   // negative
        S = 1;
    } else {
        S = 0;          // positive
    }
    if ((val & 0xff) == 0)
        Z = 1;
    else
        Z = 0;
    AC = 0;             // only true w/8080, not for Z80
    P = parity(val);

}

/* set flags after logical op */
void setlogical(int32_t reg)
{
    C = 0;      // always  (AND A opcode will clear CY)
    if (reg & 0x80) {
        S = 1;
    } else {
        S = 0;
    }
    if ((reg & 0xff) == 0)
        Z = 1;
      else
        Z = 0;
    AC = 0;
    P = parity(reg);
}

/* set flags after INR/DCR operation (8 bit only)*/
void setinc(int reg) {
    if (reg & 0x80) {
        S = 1;
    } else {
        S = 0;
    }
    if ((reg & 0xff) == 0)
        Z = 1;
      else
        Z = 0;
        
    P = parity(reg);

}

/* Put a value into an 8080 register pair */
void putpair(int16_t reg, uint16_t val) // was int16_t val
{
    switch (reg) {
        case 0:
            BC = val;
            break;
        case 1:
            DE = val;
            break;
        case 2:
            HL = val;
            break;
        case 3:
            StackP = val;
            break;
        default:
            break;
    }
}

/* Return the value of a selected register pair */
int16_t getpair(int16_t reg)
{
    switch (reg) {
        case 0:
            return (BC);
        case 1:
            return (DE);
        case 2:
            return (HL);
        case 3:
            return (StackP);
        default:
            break;
    }
    return 0;
}

/* Put a value into an 8080 register from memory */
void putreg(uint16_t reg, uint16_t val)
{
    switch (reg) {
        case 0:
            BC = BC & 0x00FF;
            BC = BC | (val <<8);
            break;
        case 1:
            BC = BC & 0xFF00;
            BC = BC | val;
            break;
        case 2:
            DE = DE & 0x00FF;
            DE = DE | (val <<8);
            break;
        case 3:
            DE = DE & 0xFF00;
            DE = DE | val;
            break;
        case 4:
            HL = HL & 0x00FF;
            HL = HL | (val <<8);
            break;
        case 5:
            HL = HL & 0xFF00;
            HL = HL | val;
            break;
        case 6:
            RAM[HL] = val & 0xff;
            break;
        case 7:
            A = val & 0xff;
        default:
            break;
    }
}

/* Get an 8080 register and return the value */
uint16_t getreg(uint16_t reg) {
    switch (reg) {
        case 0:     // B
            return ((BC >>8) & 0x00ff);
        case 1:     // C
            return (BC & 0x00ff);
        case 2:     // D
            return ((DE >>8) & 0x00ff);
        case 3:     // E
            return (DE & 0x00ff);
        case 4:     // H
            return ((HL >>8) & 0x00ff);
        case 5:     // L
            return (HL & 0x00ff);
        case 6:     // (HL)
            return RAM[HL];
        case 7:
            return (A);
        default:
            break;
    }
    return 0;
}

void reset() {      // reset the processor
    
    /* clear flags, set Program Counter to 0 */
    Z = C = S = AC = P = INTE = 0;
    PC = 0;
    
    /* turn off indicators */
    digitalWrite(RUNLED,0);
    digitalWrite(HALTLED,0);
    digitalWrite(ATTNLED,0);

    return;
}

void xmodem() {   /* load a file over the serial line, save to SD card */
    char ch, block[135], checksum, filename[24];
    uint8_t counter = 0;
    #define EOT 0x04
    #define NAK 0x15
    #define ACK 0x06
    #define SOH 0x01
    
    /* this routine is an XMODEM receiver */

    Console.print("\r\nEnter filename: ");      // ask user for filename to save data to
    while (!Console.available()) ;
    while (Console.available()) {
        ch = Console.read();
        if (ch == '\n') break;
        filename[counter++] = ch;
    }
    while (Console.available());    // empty buffer
    
    File sdfile;
    sdfile = SD.open(filename,FILE_WRITE);
    if (!sdfile) {
        Console.print("\r\nError creating"); Console.print(filename); Console.print("\r\n"); Console.flush();
        return;
    }

rxLoop:
    /* send NAK to start the transfer */
    while (1) {     // timing loop - send NAK every 3 seconds until we time out or receive a char
        Console.write(0x15);
        elapsedMillis serialTimeout;
        while (serialTimeout < 3000) {
            if (Console.available()) break;   
        }
        if (serialTimeout > 3000) {
            counter += 1;
            if (counter > 15) {
                Console.print("\r\nReceiver timed out\r\n");
                goto timedOut;
            }
        }
        if (Console.available()) {  // received a reply
            goto gotAck;
        }
        continue;
    }
    timedOut:       // no response from sender
    Console.print("\r\nNo response - stopping\r\n");
    sdfile.close();
    return;

    gotAck:         // received a response, receive 132 bytes/packet
    counter = 0;
    while (counter < 132) {
        if (Console.available())
            ch = Console.read();
            if (ch == EOT) goto rxDone;
            block[counter++] = ch;
    }
    if (block[0] != SOH){        // bad block - send NAK
        Console.write(NAK);
        goto rxLoop;
    }
    /* save block */
    for (int n=2; n<131; n++) sdfile.write(block[n]);
    Console.write(ACK);
    goto rxLoop;

rxDone:     /* received EOT - transfer complete */
    sdfile.flush();
    sdfile.close();
    delay(3000);        // let xmodem transmitter exit before sending any messages 
    Console.print("\r\nTransfer Complete\r\n");
    return;     // done
    
}




void setup() {
    Console.begin(9600);
    while (!Console) {
        ; // wait for serial port
    }
    
    //Serial1.begin(SERIAL1_PORT_SPEED);

    Printer.begin(PrinterBaudRate); // set up the serial port for the printer

    /* setup the hardware */

    pinMode(ABORTPIN, INPUT_PULLUP);
    pinMode(INTPIN, INPUT_PULLUP);
    pinMode(RESETPIN, INPUT_PULLUP);
    pinMode(RUNLED, OUTPUT);
    digitalWrite(RUNLED, 0);        // initial state=off
    pinMode(HALTLED, OUTPUT);
    digitalWrite(HALTLED, 0);       // initial state=off
    pinMode(ATTNLED, OUTPUT);
    digitalWrite(ATTNLED, 0);       // initial state=off
    pinMode(LOADPIN, INPUT_PULLUP);
    pinMode(LINE31,INPUT);          // input line 0x13
    pinMode(LINE32,INPUT);          // input line 0x12
    pinMode(LINE33,OUTPUT);         // output line 0x14
    digitalWrite(LINE33,0);         // initial output value = 1
    pinMode(LINE34,OUTPUT);         // output line 0x15
    digitalWrite(LINE34,0);         // initial output value = 1


    /* initialize built-in SD card */
    if (!SD.begin(chipSelect)) {
        Console.println("SD Card not initialized");
        Console.flush();
        abortRun();
    }

    /* PSram test */
    strcpy(PStest,"\r\n--- PSram Initialized ---\r\n");
    Serial.println(PStest);

    
}

void loop() {

    /* set up the RAM for the 8080 */
    RAM = (uint8_t *)malloc(RAMSIZE * sizeof(uint8_t));
    if (RAM == NULL) {
        digitalWrite(ATTNLED, 1);    // show problem
        Console.print("Error - unable to define memory for RAM\n");
        while(1);
    }

    /* 
    Since we're starting out, initialize the RAM with 0 
    (the 8080 doesn't do this, but this eliminates runaway code 
    that could damage attached hardware to the IO ports.
    */
    
    for (int a=0; a<RAMSIZE; a++) RAM[a] = 0;

    /* if defined, load this test program into RAM */
    #ifdef TESTCODE_LOCAL
    // this routine spits out test chrs to output port 0 forever
    uint8_t debugCode[] = {\
    0x3e,0x20,0x0e,0x5e,0xd3,0x00,0x3c,0x0d,0xc2,0x04,0x00,\
    0x3e,0x0d,0xd3,0x00,0x3e,0x0a,0xd3,0x00,0xc3,0x00,0x00\
    };
    for (int n=0;n<22;n++) RAM[n]=debugCode[n];     // since this starts at 0x00 we don't worry about banking
    #endif


    /* if defined, load test program LOAD.COM from the SD card into RAM */
    #ifdef TESTCODE_DISK
    Console.println("Loading load.com from sd card");
    Console.flush();
    loadFile = SD.open("LOAD.COM");
    if (loadFile) {
        Console.println("file LOAD.COM opened");
        Console.flush();
        /* read from the file, save to memory */
        PC = 0;
        while (loadFile.available()) {
            #ifdef BANKDEF
                RAM[PC<32768 ? PC : (((32768*BANK)+PC) & 0x3ffff)] = loadFile.read(); 
                PC++;
            #endif
            #ifndef BANKDEF 
                RAM[PC++] = loadFile.read();
            #endif
        }
        loadFile.close();
    } else {
        Console.println("failed to open LOAD.COM");
        Console.flush();
    }   
    #endif


    /* look for the file BOOTCODE.COM on the root of the SD card. Load into memory
     *  if it exists.
    */
     FLAG = 0;
     Console.println("Searching for OS80.COM");
     Console.flush();
     loadFile = SD.open("OS80.COM");
     if (loadFile) {
        digitalWrite(ATTNLED,1);
        Console.println("file OS80.COM opened");
        Console.flush();
        /* read from the file, save to memory */
        PC = 0;
        while (loadFile.available()) {
            #ifdef BANKDEF
                RAM[PC<32768 ? PC : (((32768*BANK)+PC) & 0x3ffff)];
                PC++;
            #endif
            #ifndef BANKDEF
                RAM[PC++] = loadFile.read();
            #endif
        }
        Console.print("Loaded "); Console.print(PC-1, DEC); Console.println(" bytes into RAM");
        loadFile.close();
        Console.println("file OS80.COM loaded successfully");
        Console.flush();
    } else {
        Console.println("failed to load OS80.COM");
        Console.flush();
        digitalWrite(ATTNLED,0);
        FLAG = 1;       // 1=>no code loaded, jump to monitor routine, let user load code manually
    }
    

    

    /* this runs once */
    Console.println("-- 8080 Emulator Starting --");
    if (FLAG == 1) {
        Console.println("No startup code found.");
    }
    Console.flush();
    reset();            // resets PC to 0h, clears flags

    if (FLAG) abortRun();      // give the user a chance to load code. Resume from here.
    digitalWrite(RUNLED,1);
    digitalWrite(HALTLED,0);
    // return from abortRun(), start the program loop

    /*********************/
    /* main program loop */
    /*********************/
    while (1) {

        digitalWrite(RUNLED,1);
        
        /* test if RESET button pressed */
        if (digitalRead(RESETPIN)==0) {     // reset pressed
            reset();
            while (digitalRead(RESETPIN)==0) continue; // wait until released
            delay(DEBOUNCE);
            continue;
        }


        /* test if ABORT button pressed */
        if (digitalRead(ABORTPIN)==0) {       // abort pressed
            digitalWrite(RUNLED,0);
            digitalWrite(HALTLED,1);
            Console.println("\r\nABORT button pressed");
            abortRun();                       // jump to the monitor program
            while (digitalRead(ABORTPIN)==0) continue;    // wait until released
            delay(DEBOUNCE);
            digitalWrite(HALTLED,0);
            continue;
        }

        /* test INT pin */
        if (digitalRead(INTPIN)==0 && INTE) {      // service 8080 interrupt
            INTE = 0;
            // service int routine
            INTE = 1;
            continue;
        }


        /* test for LOAD button push */
        if (digitalRead(LOADPIN)==0) {          // load file from remote device
            digitalWrite(RUNLED,0);
            digitalWrite(HALTLED,1);
            xmodem();
            while (digitalRead(LOADPIN)==0) continue;
            delay(DEBOUNCE);
            digitalWrite(RUNLED,1);
            digitalWrite(HALTLED,0);
            continue;
        }


        // Get next opcode from RAM
        #ifdef BANKDEF
        OP = RAM[PCB];
        #endif
        #ifndef BANKDEF
        OP = RAM[PC];  //get next byte from RAM
        #endif

        /* This is the instruction decoder. Decoding style is derived from
         * https://github.com/simh/simh/blob/master/ALTAIR/altair_cpu.c
         * since it's smaller and cleaner than my orig code. I cleaned
         * up some things for this build, and tightened some code. Any 
         * errors are my own.
        */

        if (OP == 0x76) {   // HLT - stop the processor until reset
            Console.println("\r\n8080 HALT instruction executed");
            abortRun();     // jump to Monitor routine
            continue;
        }

        if ((OP & 0xC0)==0x40) {                    // MOV DEST,SRC
            temp = getreg(OP & 0x07);               // read from reg
            putreg((OP >> 3) & 0x07, temp);         // save to reg
            PC += 1;
            continue;
        }
        
        if ((OP & 0xC7)==0x06) {                    // MVI nn
            #ifdef BANKDEF
            putreg((OP >> 3) & 0x07, RAM[PCB+1]);
            #endif
            #ifndef BANKDEF
            putreg((OP >> 3) & 0x07, RAM[PC+1]);
            #endif
            PC += 2;
            continue;
        }

        if ((OP & 0xCF) == 0x01) {                  // LXI nn
            temp = RAM[++PC];
            temp += RAM[++PC] * 256;
            putpair(((OP >> 4) & 0x03), temp);
            PC += 1;
            continue;
        }

        if ((OP & 0xEF) == 0x0A) {                  // LDAX
            temp = getpair((OP >> 4) & 0x03);
            putreg(7, RAM[temp]);
            PC += 1;
            continue;
        }

        if ((OP & 0xEF) == 0x02) {                  // STAX
            temp = getpair((OP >> 4) & 0x03);
            RAM[temp] = getreg(7); 
            PC += 1;
            continue;
        }

                /* opcodes with tests */
        
        if ((OP & 0xF8) == 0xB8) {                  // CMP
            temp = A & 0xFF;
            temp -= getreg(OP & 0x07);
            setarith(temp);
            PC += 1;
            continue;
        }

        if ((OP & 0xC7) == 0xC2) {                  // JMP <condition>
            if (cond((OP >> 3) & 0x07) == 1) {
                lo = RAM[++PC];
                hi = RAM[++PC];
                PC = (hi << 8) + lo;
            } else {
                PC += 3;
            }
            continue;
        }

        if ((OP & 0xC7) == 0xC4) {                  // CALL <condition>
            if (cond((OP >> 3) & 0x07) == 1) {
                lo = RAM[++PC];
                hi = RAM[++PC];
                PC++;
                RAM[--StackP] = (PC & 0xff00) >> 8;
                RAM[--StackP] = (PC & 0xff);
                PC = (hi * 256) + lo;
            } else {
                PC += 3;
            }
            continue;
        }

        if ((OP & 0xC7) == 0xC0) {                  // RET <condition>
            if (cond((OP >> 3) & 0x07) == 1) {
                PC = RAM[StackP];
                StackP++;
                PC |= ((RAM[StackP] << 8) & 0xff00);
                StackP++;
            } else {
                PC += 1;
            }
            continue;
        }

        if ((OP & 0xC7) == 0xC7) {                  // RST
            StackP--;
            RAM[StackP] = (PC >> 8) & 0xff;
            StackP--;
            RAM[StackP] = PC & 0xff;
            PC = OP & 0x38;
            continue;
        }

        if ((OP & 0xCF) == 0xC5) {                  // PUSH
            temp = getpush((OP >> 4) & 0x03);
            StackP--;
            RAM[StackP] = (temp >> 8) & 0xff;
            StackP--;
            RAM[StackP] =  temp & 0xff;
            PC += 1;
            continue;
        }

        if ((OP & 0xCF) == 0xC1) {                   // POP
            temp = RAM[StackP];
            StackP++;
            temp |= RAM[StackP] << 8;
            StackP++;
            putpush((OP >> 4) & 0x03, temp);
            PC += 1;
            continue;
        }

         if ((OP & 0xF8) == 0x80) {                  // ADD
            A += getreg(OP & 0x07);
            setarith(A);
            A = A & 0xFF;
            PC += 1;
            continue;
        }

        if ((OP & 0xF8) == 0x88) {                  // ADC
            carry = 0;
            if (C) carry = 1;
            A += getreg(OP & 0x07);
            A += carry;
            setarith(A);
            A = A & 0xFF;
            PC += 1;
            continue;
        }

        if ((OP & 0xF8) == 0x90) {                  // SUB
            A -= getreg(OP & 0x07);
            setarith(A);
            A = A & 0xFF;
            PC += 1;
            continue;
        }

        if ((OP & 0xF8) == 0x98) {                  // SBB
            carry = 0;
            if (C) carry = 1;
            A = A - getreg(OP & 0x07) - carry;
            setarith(A);
            A = A & 0xFF;
            PC += 1;
            continue;
        }

         if ((OP & 0xC7) == 0x04) {                  // INR
            temp = getreg((OP >> 3) & 0x07);
            temp++;
            setinc(temp);
            temp = temp & 0xFF;
            putreg((OP >> 3) & 0x07, temp);
            PC += 1;
            continue;
        }

        if ((OP & 0xC7) == 0x05) {                  // DCR
            temp = getreg((OP >> 3) & 0x07);
            temp--;
            setinc(temp);
            temp = temp & 0xFF;
            putreg((OP >> 3) & 0x07, temp);
            PC += 1;
            continue;
        }

        if ((OP & 0xCF) == 0x03) {                  // INX
            temp = getpair((OP >> 4) & 0x03);
            temp++;
            temp = temp & 0xFFFF;
            putpair((OP >> 4) & 0x03, temp);
            PC += 1;
            continue;
        }

        if ((OP & 0xCF) == 0x0B) {                  // DCX
            temp = getpair((OP >> 4) & 0x03);
            temp--;
            temp = temp & 0xFFFF;
            putpair((OP >> 4) & 0x03, temp);
            PC += 1;
            continue;
        }

        if ((OP & 0xCF) == 0x09) {                  // DAD
            C = 0;
            if (long(HL) + long(getpair((OP >> 4) & 0x03) > 0xffff))
                C = 1;
            HL += getpair((OP >> 4) & 0x03);
            PC += 1;
            continue;
        }

        if ((OP & 0xF8) == 0xA0) {                  // ANA
            A &= getreg(OP & 0x07);
            C = 0;
            setlogical(A);
            A &= 0xFF;
            PC += 1;
            continue;
        }

        if ((OP & 0xF8) == 0xB0) {                  // ORA
            A |= getreg(OP & 0x07);
            C = 0;
            setlogical(A);
            A &= 0xFF;
            PC += 1;
            continue;
        }

        if ((OP & 0xF8) == 0xA8) {                  // XRA
            A ^= getreg(OP & 0x07);
            C = 0;
            setlogical(A);
            A &= 0xFF;
            PC += 1;
            continue;
        }

        /* now do the rest of the instructions */

        switch(OP) {

            case 0xfe: {                        // CPI
                temp = A & 0xFF;
                temp -= RAM[++PC];
                PC += 1;
                setarith(temp);
                break;
            }


            case 0xe6: {                        // ANI
                A &= RAM[++PC];
                PC += 1;
                C = AC = 0;
                setlogical(A);
                A &= 0xFF;
                break;
            }

            case 0xee: {                        // XRI
                A ^= RAM[++PC];
                PC += 1;
                C = AC = 0;
                setlogical(A);
                A &= 0xFF;
                break;
            }

            case 0xf6: {                        // ORI
                A |= RAM[++PC];
                PC += 1;
                C = AC = 0;
                setlogical(A);
                A &= 0xFF;
                break;
            }

            /* Jump/Call instructions */
            
            case 0xc3: {                        // JMP
                lo = RAM[++PC];
                hi = RAM[++PC];
                PC = (hi << 8) + lo;
                break;
            }

            case 0xe9: {                        // PCHL
                PC = HL;
                break;
            }

            case 0xcd: {                        // CALL

                lo = RAM[++PC];
                hi = RAM[++PC];
                PC++;       // point to address after call
                --StackP;
                RAM[StackP] = (PC & 0xff00) >> 8;
                --StackP;
                RAM[StackP] =  (PC & 0xff);
                PC = (hi * 256) + lo;
                break;
            }

            case 0xc9: {                        // RET
            PC = RAM[StackP];
            StackP++;
            PC |= ((RAM[StackP] << 8) & 0xff00);
            StackP++;
            break;
            }

            /* Data Transfer instructions */
            
            case 0x32: {                        // STA
                lo = RAM[++PC];
                hi = RAM[++PC];
                PC += 1;
                temp = (hi << 8) + lo;
                RAM[temp] = A;
                break;
            }

            case 0x3a: {                        // LDA
                lo = RAM[++PC];
                hi = RAM[++PC];
                PC += 1;
                temp = (hi << 8) + lo;
                A = RAM[temp];
                break;
            }

            case 0x22: {                        // SHLD
                lo = RAM[++PC];
                hi = RAM[++PC];
                PC += 1;
                temp = (hi << 8) + lo;
                RAM[temp] = HL;
                temp++;
                RAM[temp] = (HL >>8) & 0x00ff;
                break;
            }

            case 0x2a: {                        // LHLD
                lo = RAM[++PC];
                hi = RAM[++PC];
                PC += 1;
                temp = (hi << 8) + lo;
                HL = RAM[temp];
                temp++;
                HL = HL | (RAM[temp] << 8);
                break;
            }

            case 0xeb: {                        // XCHG
                temp = HL;
                HL = DE;
                DE = temp;
                PC += 1;
                break;
            }

            /* Arithmetic Instructions */
            
            case 0xc6: {                        // ADI
                A += RAM[++PC];
                PC += 1;
                setarith(A);
                A = A & 0xFF;
                break;
            }

            case 0xce: {                        // ACI
                carry = 0;
                if (C) carry = 1;
                A += RAM[++PC];
                A += carry;
                PC += 1;
                setarith(A);
                A = A & 0xFF;
                break;
            }

            case 0xd6: {                        // SUI
                A -= RAM[++PC];
                PC += 1;
                setarith(A);
                A = A & 0xFF;
                break;
            }

            case 0xde: {                        // SBI
                carry = 0;
                if (C) carry = 1;
                A -= (RAM[++PC] + carry);
                PC += 1;
                setarith(A);
                A = A & 0xFF;
                break;
            }

            case 0x27: {                        // DAA
                uint8_t a_hi, a_lo;
                a_lo = A & 0x0F; 
                a_hi = (A >> 4) & 0x0F;
                
                if (a_lo > 9 || AC == 1) {
                    a_lo += 6;
                    if (a_lo > 0xf) {
                        C = 1;
                        a_hi += 1;
                        
                    }
                }
                a_lo &= 0x0f; 

                if (a_hi > 9 ) a_hi += 6;
                if (a_hi > 15) 
                    C = 1;
                else
                    C = 0;
                
                a_hi &= 0xf;
                    
                A = (a_hi << 4);
                A |= a_lo;
                A &= 0xFF;

                Z = 0;
                if (A == 0) Z = 1;
                P = parity(A);
                S = 0;
                if (A > 0x80) S = 1;
                PC += 1;
                break;    
            
            }

            case 0x07: {                        // RLC
                C = 0;
                A = (A << 1);
                if (A & 0x100) C = 1;
                A &= 0xFF;
                if (C)
                    A |= 0x01;
                PC += 1;
                break;
            }

            case 0x0f: {                        // RRC
                C = 0;
                if ((A & 0x01) == 1)
                    C = 1;
                A = (A >> 1) & 0xFF;
                if (C)
                    A |= 0x80;
                PC += 1;
                break;
            }

            case 0x17: {                        // RAL
                temp = C;
                C = 0;
                A = (A << 1);
                if (A & 0x100) C = 1;
                A &= 0xFF;
                if (temp)
                    A |= 1;
                else
                    A &= 0xFE;
                PC += 1;
                break;
            }

            case 0x1f: {                        // RAR
                temp = C;
                C = 0;
                if ((A & 0x01) == 1)
                    C = 1;
                A = (A >> 1) & 0xFF;
                if (temp)
                    A |= 0x80;
                else
                    A &= 0x7F;
                PC += 1;
                break;
            }

            case 0x2f: {                        // CMA
                A = ~ A;
                A &= 0xFF;
                PC += 1;
                break;
            }

            case 0x3f: {                        // CMC
                C = ~ C;
                PC += 1;
                break;
            }

            case 0x37: {                        // STC
                C = 1;
                PC += 1;
                break;
            }

            /* Stack and Control Group */
            
            case 0xe3: {                        // XTHL
                lo = RAM[StackP];
                hi = RAM[StackP + 1];
                RAM[StackP] = HL & 0xFF; 
                RAM[StackP+1] = (HL >> 8) & 0xFF;
                HL = (hi << 8) + lo;
                PC += 1;
                break;
            }

            case 0xf9: {                        // SPHL
                StackP = HL;
                PC += 1;
                break;
            }

            case 0xfb: {                        // EI
                INTE = 1;
                PC += 1;
                break;
            }

            case 0xf3: {                        // DI
                INTE = 0;
                PC += 1;
                break;
            }

            case 0xd3:  {                       // OUT
                #ifdef BANKDEF
                output(a,RAM[++(PC>32768 ? PC : (((32768*BANK)+PC) & 0x3fff))];
                #endif
                #ifndef BANKDEF
                output(A,RAM[++PC]);            // value/address
                #endif
                PC += 1;
                break;
            }

            case 0xdb:  {                       // IN
                #ifdef BANKDEF
                A = input(RAM[++(PC>32768 ? PC : (((32768*BANK)+PC) & 0x3fff))]
                #endif
                #ifndef BANKDEF
                A = input(RAM[++PC]);           // address
                #endif
                A &= 0xff;
                PC += 1;
                break;
            }

            default:
                PC += 1;        // NOP & unused/invalid opcodes are ignored
                break;

        }   // end of case statements

        continue;       // end of main while loop
        
    }

}
