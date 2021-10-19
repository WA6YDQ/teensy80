/* drivers.h - simulated hardware drivers for 80sim.ino
 *
 * NOTE: the Altair has Serial I/O at:
 *  0x10 (status port 0, console status, input only, return count of avail chars)
 *  0x11 (data port 0, console device in/out (byte at a time)
 *  0x12 (status port 1, input tape reader, output punch device (paper tape))
 *  0x13 (data port 1, input tape reader, output punch device (paper tape))
 *  
 *  And disk units at:
 *  0x08 (disk device octal 10, output - Select/enable controller & drive)
 *  0x08 (disk device octal 10, input  - Return status of drive and controller)
 *  0x09 (disk device octal 11, output - Control disk function)
 *  0x09 (disk device octal 11, input  - Indicates current sector position of disk)
 *  0x0a (disk device octal 12, output - Write data to disk)
 *  0x0a (disk device octal 12, input  - Read data from disk)
 *  
 *  (see https://github.com/simh/simh/blob/master/ALTAIR/altair_dsk.c for full specs)
 */

/* input routines */
uint8_t input(uint8_t addr) {

    if (addr == 0x10) {     // Altair serial console status - return 0 or number of chars available
        return(Serial.available());
    }

    if (addr == 0x11) {     // Altair serial console input port - return char each time called
        return(Serial.read()); 
    }

    
    return 0;
}




/* output routines: send a value to a specified address */
void output(uint8_t val, uint8_t addr) {

    if (addr == 0x10) {     // test status for Altair console output device (always 1=ready)
        return;
    }

    if (addr == 0 || addr == 1 || addr == 0x11) {       // serial output to console device
        digitalWrite(IOLED,1);      // light up front panel status indicator
        Serial.write(val);
        digitalWrite(IOLED,0);      // and turn off indicator
        return;
    }




    //------ unknown device selected if we get here -----
    return;
}
