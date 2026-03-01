/*
 * The MIT License
 *
 * Copyright 2020 The OpenNARS authors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "NAR.h"
#include "./unit_tests/unit_tests.h"
#include "./system_tests/system_tests.h"
#include "Shell.h"

void Process_Args(NAR_t *nar, int argc, char *argv[])
{
    if(argc >= 2)
    {
        NAR_INIT(nar);
        if(!strcmp(argv[1],"NAL_GenerateRuleTable"))
        {
            NAL_GenerateRuleTable(nar);
            exit(0);
        }
        if(!strcmp(argv[1],"test"))
        {
            Run_Unit_Tests(nar);
            Run_System_Tests(nar);
            puts("All tests passed.");
            exit(0);
        }
        if(!strcmp(argv[1],"shell"))
        {
            Shell_Start(nar);
        }
    }
}

void Display_Help(void)
{
    puts("Usage:");
    puts("  driftnars test   — run unit and system tests");
    puts("  driftnars shell  — interactive Narsese REPL");
}

int main(int argc, char *argv[])
{
    NAR_t *nar = NAR_New();
    assert(nar != NULL, "Failed to allocate NAR instance");
#ifdef SEED
    mysrand(nar, SEED);
#else
    mysrand(nar, 666);
#endif
    Process_Args(nar, argc, argv);
    if(argc == 1)
    {
        Display_Help();
    }
    NAR_Free(nar);
    return 0;
}
