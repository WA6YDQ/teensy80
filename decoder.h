/* The next two vars are for the dissassembler */

const char *opcode[] = {
"NOP", "LXI B", "STAX B", "INX B",                      /* 000-003 */
"INR B", "DCR B", "MVI B", "RLC",                       /* 004-007 */
"???", "DAD B", "LDAX B", "DCX B",                      /* 010-013 */
"INR C", "DCR C", "MVI C", "RRC",                       /* 014-017 */
"???", "LXI D", "STAX D", "INX D",                      /* 020-023 */
"INR D", "DCR D", "MVI D", "RAL",                       /* 024-027 */
"???", "DAD D", "LDAX D", "DCX D",                      /* 030-033 */
"INR E", "DCR E", "MVI E", "RAR",                       /* 034-037 */
"???", "LXI H", "SHLD", "INX H",                        /* 040-043 */
"INR H", "DCR H", "MVI H", "DAA",                       /* 044-047 */
"???", "DAD H", "LHLD", "DCX H",                        /* 050-053 */
"INR L", "DCR L", "MVI L", "CMA",                       /* 054-057 */
"???", "LXI SP", "STA", "INX SP",                       /* 060-063 */
"INR M", "DCR M", "MVI M", "STC",                       /* 064-067 */
"???", "DAD SP", "LDA", "DCX SP",                       /* 070-073 */
"INR A", "DCR A", "MVI A", "CMC",                       /* 074-077 */
"MOV B,B", "MOV B,C", "MOV B,D", "MOV B,E",             /* 100-103 */
"MOV B,H", "MOV B,L", "MOV B,M", "MOV B,A",             /* 104-107 */
"MOV C,B", "MOV C,C", "MOV C,D", "MOV C,E",             /* 110-113 */
"MOV C,H", "MOV C,L", "MOV C,M", "MOV C,A",             /* 114-117 */
"MOV D,B", "MOV D,C", "MOV D,D", "MOV D,E",             /* 120-123 */
"MOV D,H", "MOV D,L", "MOV D,M", "MOV D,A",             /* 124-127 */
"MOV E,B", "MOV E,C", "MOV E,D", "MOV E,E",             /* 130-133 */
"MOV E,H", "MOV E,L", "MOV E,M", "MOV E,A",             /* 134-137 */
"MOV H,B", "MOV H,C", "MOV H,D", "MOV H,E",             /* 140-143 */
"MOV H,H", "MOV H,L", "MOV H,M", "MOV H,A",             /* 144-147 */
"MOV L,B", "MOV L,C", "MOV L,D", "MOV L,E",             /* 150-153 */
"MOV L,H", "MOV L,L", "MOV L,M", "MOV L,A",             /* 154-157 */
"MOV M,B", "MOV M,C", "MOV M,D", "MOV M,E",             /* 160-163 */
"MOV M,H", "MOV M,L", "HLT", "MOV M,A",                 /* 164-167 */
"MOV A,B", "MOV A,C", "MOV A,D", "MOV A,E",             /* 170-173 */
"MOV A,H", "MOV A,L", "MOV A,M", "MOV A,A",             /* 174-177 */
"ADD B", "ADD C", "ADD D", "ADD E",                     /* 200-203 */
"ADD H", "ADD L", "ADD M", "ADD A",                     /* 204-207 */
"ADC B", "ADC C", "ADC D", "ADC E",                     /* 210-213 */
"ADC H", "ADC L", "ADC M", "ADC A",                     /* 214-217 */
"SUB B", "SUB C", "SUB D", "SUB E",                     /* 220-223 */
"SUB H", "SUB L", "SUB M", "SUB A",                     /* 224-227 */
"SBB B", "SBB C", "SBB D", "SBB E",                     /* 230-233 */
"SBB H", "SBB L", "SBB M", "SBB A",                     /* 234-237 */
"ANA B", "ANA C", "ANA D", "ANA E",                     /* 240-243 */
"ANA H", "ANA L", "ANA M", "ANA A",                     /* 244-247 */
"XRA B", "XRA C", "XRA D", "XRA E",                     /* 250-253 */
"XRA H", "XRA L", "XRA M", "XRA A",                     /* 254-257 */
"ORA B", "ORA C", "ORA D", "ORA E",                     /* 260-263 */
"ORA H", "ORA L", "ORA M", "ORA A",                     /* 264-267 */
"CMP B", "CMP C", "CMP D", "CMP E",                     /* 270-273 */
"CMP H", "CMP L", "CMP M", "CMP A",                     /* 274-277 */
"RNZ", "POP B", "JNZ", "JMP",                           /* 300-303 */
"CNZ", "PUSH B", "ADI", "RST 0",                        /* 304-307 */
"RZ", "RET", "JZ", "???",                               /* 310-313 */
"CZ", "CALL", "ACI", "RST 1",                           /* 314-317 */
"RNC", "POP D", "JNC", "OUT",                           /* 320-323 */
"CNC", "PUSH D", "SUI", "RST 2",                        /* 324-327 */
"RC", "???", "JC", "IN",                                /* 330-333 */
"CC", "???", "SBI", "RST 3",                            /* 334-337 */
"RPO", "POP H", "JPO", "XTHL",                          /* 340-343 */
"CPO", "PUSH H", "ANI", "RST 4",                        /* 344-347 */
"RPE", "PCHL", "JPE", "XCHG",                           /* 350-353 */
"CPE", "???", "XRI", "RST 5",                           /* 354-357 */
"RP", "POP PSW", "JP", "DI",                            /* 360-363 */
"CP", "PUSH PSW", "ORI", "RST 6",                       /* 364-367 */
"RM", "SPHL", "JM", "EI",                               /* 370-373 */
"CM", "???", "CPI", "RST 7",                            /* 374-377 */
 };

uint8_t oplen[256] = {
1,3,1,1,1,1,2,1,0,1,1,1,1,1,2,1,0,3,1,1,1,1,2,1,0,1,1,1,1,1,2,1,
0,3,3,1,1,1,2,1,0,1,3,1,1,1,2,1,0,3,3,1,1,1,2,1,0,1,3,1,1,1,2,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,3,3,3,1,2,1,1,1,3,0,3,3,2,1,1,1,3,2,3,1,2,1,1,0,3,2,3,0,2,1,
1,1,3,1,3,1,2,1,1,1,3,1,3,0,2,1,1,1,3,1,3,1,2,1,1,1,3,1,3,0,2,1 };
