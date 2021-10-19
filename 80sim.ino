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

#define DEBUG 0
#define RAMSIZE 65536
#define DEBOUNCE 80             // msec delay for debouncing a switch
#define SERIAL1_PORT_SPEED 9600 // baud rate of db-9 serial port
#define Console Serial

// hardware (lights, switches)
#define ABORTPIN 2      // momentary switch input for INT switch
#define RESETPIN 3      // momentary switch input for RESET switch
#define DISKLED  6      // LED output (active 1) when disk activity happening
#define IOLED    7      // LED output (active 1) when any IO happening
#define ACTLED   9      // LED output (active 1) when some routine needs to alert the user
#define INTPIN   8      // Interrupt pin for 8080 (depends on INTE)



/* defines for SD card on teensy 4.1 */
#include <SD.h>
#include <SPI.h>
const int chipSelect = BUILTIN_SDCARD;
File loadFile;

#include <string.h>
#include "drivers.h"        // specific hardware drivers for 80sim
#include "decoder.h"        // op code decoding
#include "monitor.h"        // abort monitor routines

/* define the registers */
uint16_t    PC, A, BC, DE, HL, StackP, temp, carry, hi, lo;
uint8_t     OP;
bool        C, Z, P, AC, S, INTE;

/* define main memory */
uint8_t *RAM;

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
    digitalWrite(DISKLED,0);
    digitalWrite(IOLED,0);
    digitalWrite(ACTLED,0);

    return;
}


void setup() {
    Serial.begin(9600);
    while (!Serial) {
        ; // wait for serial port
    }
    
    //Serial1.begin(SERIAL1_PORT_SPEED);

    /* setup the hardware */

    pinMode(ABORTPIN, INPUT_PULLUP);
    pinMode(INTPIN, INPUT_PULLUP);
    pinMode(RESETPIN, INPUT_PULLUP);
    pinMode(DISKLED, OUTPUT);
    digitalWrite(DISKLED, 0);       // initial state=off
    pinMode(IOLED, OUTPUT);
    digitalWrite(IOLED, 0);         // initial state=off
    pinMode(ACTLED, OUTPUT);
    digitalWrite(ACTLED, 1);       // initial state=on


    /* initialize built-in SD card */
    if (!SD.begin(chipSelect)) {
        Serial.println("SD Card not initialized");
        Serial.flush();
        abortRun();
    }



    
}

void loop() {

    /* set up the RAM for the 8080 */
    RAM = (uint8_t *)malloc(RAMSIZE * sizeof(uint8_t));
    if (RAM == NULL) {
        digitalWrite(ACTLED, 1);    // show problem
        Serial.print("Error - unable to define memory for RAM\n");
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
    for (int n=0;n<22;n++) RAM[n]=debugCode[n];
    #endif


    /* if defined, load test program LOAD.COM from the SD card into RAM */
    #ifdef TESTCODE_DISK
    Serial.println("Loading load.com from sd card");
    Serial.flush();
    loadFile = SD.open("LOAD.COM");
    if (loadFile) {
        Serial.println("file LOAD.COM opened");
        Serial.flush();
        /* read from the file, save to memory */
        PC = 0;
        while (loadFile.available()) {
            RAM[PC++] = loadFile.read();
        }
        loadFile.close();
    } else {
        Serial.println("failed to open LOAD.COM");
        Serial.flush();
    }   
    #endif


    /* look for the file BOOTCODE.COM on the root of the SD card. Load into memory
     *  if it exists.
    */
     FLAG = 0;
     Serial.println("Searching for BOOTCODE.COM");
     Serial.flush();
     loadFile = SD.open("BOOTCODE.COM");
     if (loadFile) {
        Serial.println("file BOOTCODE.COM opened");
        Serial.flush();
        /* read from the file, save to memory */
        PC = 0;
        while (loadFile.available()) {
            RAM[PC++] = loadFile.read();
        }
        Serial.print("Loaded "); Serial.print(PC-1, DEC); Serial.println(" bytes into RAM");
        loadFile.close();
        Serial.println("file BOOTCODE.COM loaded successfully");
        Serial.flush();
    } else {
        Serial.println("failed to load BOOTCODE.COM");
        Serial.flush();
        FLAG = 1;       // 1=>no code loaded, jump to monitor routine, let user load code manually
    }
    

    

    /* this runs once */
    Serial.println("-- 8080 Emulator Starting --");
    if (FLAG == 1) {
        Serial.println("No startup code found.");
    }
    Serial.flush();
    reset();            // resets PC to 0h

    if (FLAG) abortRun();      // give the user a chance to load code. Resume from here.
    // return from abortRun() starts the program loop

    /*********************/
    /* main program loop */
    /*********************/
    while (1) {

        /* test if RESET button pressed */
        if (digitalRead(RESETPIN)==0) {     // reset pressed
            reset();
            while (digitalRead(RESETPIN)==0) continue; // wait until released
            delay(DEBOUNCE);
            continue;
        }


        /* test if ABORT button pressed */
        if (digitalRead(ABORTPIN)==0) {       // abort pressed
            Serial.println("\r\nABORT button pressed");
            abortRun();                       // jump to the monitor program
            while (digitalRead(ABORTPIN)==0) continue;    // wait until released
            delay(DEBOUNCE);
            continue;
        }

        /* test INT pin */
        if (digitalRead(INTPIN)==0 && INTE == 1) {      // service 8080 interrupt
            while (digitalRead(INTPIN)==0) ;            // wait until high
            INTE = 0;
            // service int routine
            INTE = 1;
            continue;
        }


        OP = RAM[PC];  //get next byte from RAM


        /* This is the instruction decoder. Decoding style is lifted from
         * https://github.com/simh/simh/blob/master/ALTAIR/altair_cpu.c
         * since it's smaller and cleaner than my orig code. I cleaned
         * up some things for this build, and tightened some code. Any 
         * errors are my own.
        */

        if (OP == 0x76) {   // HLT - stop the processor until reset
            Serial.println("\r\n8080 HALT instruction executed");
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
            putreg((OP >> 3) & 0x07, RAM[PC+1]);
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
                //PC = (hi << 8) + lo;
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
                output(A,RAM[++PC]);            // value/address
                PC += 1;
                break;
            }

            case 0xdb:  {                       // IN
                A = input(RAM[++PC]);           // address
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
