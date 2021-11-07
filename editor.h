/* editor.h - line editor */

#define BUFSIZE 32000

#include <string.h>
#include <SD.h>
#include <SPI.h>



void ledit() {
    uint8_t buffer*;
    uint8_t linein[85];
    uint8_t pos, cnt;
    char ch; 
    uint16_t linecount;

    buffer = (uint8_t *)malloc(BUFSIZE * sizeof(uint8_t));
    if (buffer == NULL) {   /* not enough memory? */
        Serial.print("\r\nNot enough memory to run the editor.\r\n");
        return;
    }
    memset(buffer,0,sizeof(buffer));
    pos = 0;        /* position in the buffer of the next avail space */

ledLoop:
    /* main edit loop */
    cnt = 0;
    while (1) {
        while (!serial.available()) ;
        if (Serial.available())
            ch = Serial.read();
        else
            continue;
        Serial.print(ch); Serial.flush();
        linein[cnt++] = ch;
        if (ch == '\r' || ch == '\n') break;
        if (cnt > 80) break;    
    }
    /* either the line is > 80 chars or user hit \n or \r */
    
    /* test if command entered */
    if (strncmp(linein,".qu",3)==0) {       // exit the editor
        free(buffer);
        Serial.print("\r\nExiting editor\r\n\n");
        return;       
    }
    
    if (strncmp(linein,".sa",3)==0) ledit_save();       // save the buffer to disk
    if (strncmp(linein,".lo",3)==0) ledit_load();       // load from disk to the buffer
    if (strncmp(linein,".pr",3)==0) ledit_print();      // display the buffer
    
    if (strncmp(linein,".ne",3)==0) {       // clear the buffer
        pos = 0;        
        memset(buffer,0,sizeof(buffer));
        Serial.print("\r\nBuffer Cleared\r\n"); Serial.flush();
        goto ledloop;
    }
    
    /* no command entered, save line to buffer */
    for (int n=0; n<cnt; n++) buffer[pos+n] = linein[n];
    pos += n;
    goto ledLoop; 
    
}
