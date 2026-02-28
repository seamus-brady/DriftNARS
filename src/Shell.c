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

#include "Shell.h"
#include "Linedit.h"

static Feedback Shell_op_nop(Term args)
{
    (void) args;
    return (Feedback) {0};
}

void Shell_NARInit(NAR_t *nar)
{
    fflush(stdout);
    NAR_INIT(nar);
    nar->PRINT_DERIVATIONS = true;
}

int Shell_ProcessInput(NAR_t *nar, char *line)
{
    //trim string, for IRC etc. convenience
    for(int i=strlen(line)-1; i>=0; i--)
    {
        if(!isspace((int) line[i]))
        {
            break;
        }
        line[i] = 0;
    }
    int size = strlen(line);
    if(size==0)
    {
        NAR_Cycles(nar, 1);
    }
    else
    {
        //accept comments, commands, timestep, and narsese
        if(!strcmp(line,"help") || !strcmp(line,":help") || !strcmp(line,"*help"))
        {
            puts("DriftNARS shell — enter Narsese or commands.\n");
            puts("Narsese input:");
            puts("  <S --> P>.          Inheritance belief (eternal)");
            puts("  <S --> P>. :|:      Belief at current time");
            puts("  <S --> P>?          Question");
            puts("  <S --> P>? :|:      Question about now");
            puts("  <S --> P>! :|:      Goal (must have tense)");
            puts("  N                   Run N inference cycles");
            puts("  (empty line)        Run 1 inference cycle\n");
            puts("Commands:");
            puts("  *reset              Reset the reasoner");
            puts("  *stats              Print runtime statistics");
            puts("  *concepts           Dump all concepts");
            puts("  *opconfig           Show operation configuration");
            puts("  *volume=N           Set output volume (0-100)");
            puts("  *decisionthreshold=F  Set decision threshold");
            puts("  *motorbabbling=F    Set motor babbling chance (0.0-1.0)");
            puts("  *babblingops=N      Set number of babbling operations");
            puts("  *setopname ID NAME  Register operation name");
            puts("  *setoparg ID N TERM Set operation babbling argument");
            puts("  *concurrent         Mark next input as same timestep");
            puts("  *query T NARSESE    Query with truth expectation threshold");
            puts("  help                Show this help");
            puts("  quit                Exit the shell");
            return SHELL_CONTINUE;
        }
        else
        if(line[0] == '/' && line[1] == '/')
        {
            fputs("Comment: ", stdout);
            puts(&line[2]); fflush(stdout);
            return SHELL_CONTINUE;
        }
        else
        if(!strcmp(line,"*reset"))
        {
            return SHELL_RESET;
        }
        else
        if(!strcmp(line,"*volume=0"))
        {
            nar->PRINT_EVENTS_PRIORITY_THRESHOLD = 1.0;
        }
        else
        if(!strcmp(line,"*volume=100"))
        {
            nar->PRINT_EVENTS_PRIORITY_THRESHOLD = 0.0;
        }
        else
        if(!strncmp("*restrictedconceptcreation=true", line, strlen("*restrictedconceptcreation=true")))
        {
            nar->RESTRICTED_CONCEPT_CREATION = true;
        }
        else
        if(!strncmp("*restrictedconceptcreation=false", line, strlen("*restrictedconceptcreation=false")))
        {
            nar->RESTRICTED_CONCEPT_CREATION = false;
        }
        else
        if(!strncmp("*volume=", line, strlen("*volume=")))
        {
            int volume = 0;
            sscanf(&line[strlen("*volume=")], "%d", &volume);
            nar->PRINT_EVENTS_PRIORITY_THRESHOLD = 1.0 - ((double) volume) / 100.0;
        }
        else
        if(!strncmp("*anticipationconfidence=", line, strlen("*anticipationconfidence=")))
        {
            sscanf(&line[strlen("*anticipationconfidence=")], "%lf", &nar->ANTICIPATION_CONFIDENCE);
        }
        else
        if(!strncmp("*decisionthreshold=", line, strlen("*decisionthreshold=")))
        {
            sscanf(&line[strlen("*decisionthreshold=")], "%lf", &nar->DECISION_THRESHOLD);
        }
        else
        if(!strncmp("*similaritydistance=", line, strlen("*similaritydistance=")))
        {
            sscanf(&line[strlen("*similaritydistance=")], "%lf", &nar->similarity_distance);
        }
        else
        if(!strcmp(line,"*stats"))
        {
            puts("//*stats");
            Stats_Print(nar, nar->currentTime);
            puts("//*done");
        }
        else
        if(!strcmp(line,"*inverted_atom_index"))
        {
            InvertedAtomIndex_Print(nar);
        }
        else
        if(!strcmp(line,"*occurrence_time_index"))
        {
            OccurrenceTimeIndex_Print(nar, &nar->occurrenceTimeIndex);
        }
        else
        if(!strcmp(line,"*opconfig"))
        {
            puts("//*opconfig");
            printf("*motorbabbling=%f\n", nar->MOTOR_BABBLING_CHANCE);
            printf("*babblingops=%d\n", nar->BABBLING_OPS);
            for(int opi=0; opi<OPERATIONS_MAX; opi++)
            {
                if(nar->operations[opi].term.atoms[0])
                {
                    printf("*setopname %d ", opi+1);
                    Narsese_PrintTerm(nar, &nar->operations[opi].term);
                    puts("");
                }
                for(int oparg=0; oparg<OPERATIONS_BABBLE_ARGS_MAX; oparg++)
                {
                    if(nar->operations[opi].arguments[oparg].atoms[0])
                    {
                        printf("*setoparg %d %d ", opi+1, oparg+1);
                        Narsese_PrintTerm(nar, &nar->operations[opi].arguments[oparg]);
                        puts("");
                    }
                }
            }
        }
        else
        if(!strcmp(line,"*concepts"))
        {
            puts("//*concepts");
            for(int i=0; i<nar->concepts.itemsAmount; i++)
            {
                Concept *c = nar->concepts.items[i].address;
                assert(c != NULL, "Concept is null");
                fputs("//", stdout);
                Narsese_PrintTerm(nar, &c->term);
                printf(": { \"priority\": %f, \"usefulness\": %f, \"useCount\": %ld, \"lastUsed\": %ld, \"frequency\": %f, \"confidence\": %f, \"termlinks\": [", c->priority, nar->concepts.items[i].priority, c->usage.useCount, c->usage.lastUsed, c->belief.truth.frequency, c->belief.truth.confidence);
                Term left = Term_ExtractSubterm(&c->term, 1);
                Term left_left = Term_ExtractSubterm(&left, 1);
                Term left_right = Term_ExtractSubterm(&left, 2);
                Term right = Term_ExtractSubterm(&c->term, 2);
                Term right_left = Term_ExtractSubterm(&right, 1);
                Term right_right = Term_ExtractSubterm(&right, 2);
                fputs("\"", stdout);
                Narsese_PrintTerm(nar, &left);
                fputs("\", ", stdout);
                fputs("\"", stdout);
                Narsese_PrintTerm(nar, &right);
                fputs("\", ", stdout);
                fputs("\"", stdout);
                Narsese_PrintTerm(nar, &left_left);
                fputs("\", ", stdout);
                fputs("\"", stdout);
                Narsese_PrintTerm(nar, &left_right);
                fputs("\", ", stdout);
                fputs("\"", stdout);
                Narsese_PrintTerm(nar, &right_left);
                fputs("\", ", stdout);
                fputs("\"", stdout);
                Narsese_PrintTerm(nar, &right_right);
                fputs("\"", stdout);
                puts("]}");
                if(c->belief.type != EVENT_TYPE_DELETED)
                {
                    Memory_printAddedEvent(nar, &c->belief.stamp, &c->belief, 1, true, false, false, false, false);
                }
                for(int opi=0; opi<OPERATIONS_MAX; opi++)
                {
                    for(int h=0; h<c->precondition_beliefs[opi].itemsAmount; h++)
                    {
                        Implication *imp = &c->precondition_beliefs[opi].array[h];
                        Memory_printAddedImplication(nar, &imp->stamp, &imp->term, &imp->truth, imp->occurrenceTimeOffset, 1, true, false, false);
                    }
                }
                for(int h=0; h<c->implication_links.itemsAmount; h++)
                {
                    Implication *imp = &c->implication_links.array[h];
                    Memory_printAddedImplication(nar, &imp->stamp, &imp->term, &imp->truth, imp->occurrenceTimeOffset, 1, true, false, false);
                }
            }
            puts("//*done");
        }
        else
        if(!strcmp(line,"*cycling_belief_events"))
        {
            puts("//*cycling_belief_events");
            for(int i=0; i<nar->cycling_belief_events.itemsAmount; i++)
            {
                Event *e = nar->cycling_belief_events.items[i].address;
                assert(e != NULL, "Event is null");
                Narsese_PrintTerm(nar, &e->term);
                printf(": { \"priority\": %f, \"time\": %ld } ", nar->cycling_belief_events.items[i].priority, e->occurrenceTime);
                Truth_Print(&e->truth);
            }
            puts("//*done");
        }
        else
        if(!strcmp(line,"*cycling_goal_events"))
        {
            puts("//*cycling_goal_events");
            for(int layer=0; layer<CYCLING_GOAL_EVENTS_LAYERS; layer++)
            {
                for(int i=0; i<nar->cycling_goal_events[layer].itemsAmount; i++)
                {
                    Event *e = nar->cycling_goal_events[layer].items[i].address;
                    assert(e != NULL, "Event is null");
                    Narsese_PrintTerm(nar, &e->term);
                    printf(": {\"priority\": %f, \"time\": %ld } ", nar->cycling_goal_events[layer].items[i].priority, e->occurrenceTime);
                    Truth_Print(&e->truth);
                }
            }
            puts("//*done");
        }
        else
        if(!strcmp(line,"quit"))
        {
            return SHELL_EXIT;
        }
        else
        if(!strncmp("*babblingops=", line, strlen("*babblingops=")))
        {
            sscanf(&line[strlen("*babblingops=")], "%d", &nar->BABBLING_OPS);
        }
        else
        if(!strncmp("*currenttime=", line, strlen("*currenttime=")))
        {
            sscanf(&line[strlen("*currenttime=")], "%ld", &nar->currentTime);
        }
        else
        if(!strncmp("*stampid=", line, strlen("*stampid=")))
        {
            sscanf(&line[strlen("*stampid=")], "%ld", &nar->stamp_base);
        }
        else
        if(!strncmp("*stampimport=[", line, strlen("*stampimport=[")))
        {
            // Find the position of the first '[' character
            char *start = strchr(line, '[');
            // Find the position of the last ']' character
            char *end = strrchr(line, ']');
            // Extract the substring between the '[' and ']' characters
            char substr[1000];
            strncpy(substr, start + 1, end - start - 1);
            substr[end - start - 1] = '\0';
            // Tokenize the substring using ',' as the delimiter
            char *token = strtok(substr, ",");
            // Reset import stamp:
            nar->import_stamp = (Stamp) {0};
            // Parse each token and store it in the stamp if it fits
            int i = 0;
            while(token != NULL && i < STAMP_SIZE)
            {
                nar->import_stamp.evidentialBase[i++] = strtol(token, NULL, 10);
                token = strtok(NULL, ",");
            }
        }
        else
        if(!strcmp(line,"*motorbabbling=false"))
        {
            nar->MOTOR_BABBLING_CHANCE = 0.0;
        }
        else
        if(!strcmp(line,"*motorbabbling=true"))
        {
            nar->MOTOR_BABBLING_CHANCE = MOTOR_BABBLING_CHANCE_INITIAL;
        }
        else
        if(!strncmp("*motorbabbling=", line, strlen("*motorbabbling=")))
        {
            sscanf(&line[strlen("*motorbabbling=")], "%lf", &nar->MOTOR_BABBLING_CHANCE);
        }
        else
        if(!strncmp("*questionpriming=", line, strlen("*questionpriming=")))
        {
            sscanf(&line[strlen("*questionpriming=")], "%lf", &nar->QUESTION_PRIMING);
        }
        else
        if(!strncmp("*setvalue ", line, strlen("*setvalue ")))
        {
            int granularity = -1;
            double value = 0.0;
            char termname[ATOMIC_TERM_LEN_MAX+1] = {0};
            termname[ATOMIC_TERM_LEN_MAX-1] = 0;
            sscanf(&line[strlen("*setvalue ")], "%lf %d %" STR(ATOMIC_TERM_LEN_MAX) "s", &value, &granularity, (char*) &termname);
            assert(granularity >= 1 && granularity <= 1000, "Granularity out of bounds or parameter order not respected!");
            char termname_ext[ATOMIC_TERM_LEN_MAX+1] = {0};
            termname_ext[ATOMIC_TERM_LEN_MAX-1] = 0;
            const char* sep = termname[0] ? "_" : "";
            if(granularity <= 10)
            {
                sprintf(termname_ext, "%s%s%.1f", termname, sep, value);
            }
            else
            if(granularity <= 100)
            {
                sprintf(termname_ext, "%s%s%.2f", termname, sep, value);
            }
            else
            if(granularity <= 1000)
            {
                sprintf(termname_ext, "%s%s%.3f", termname, sep, value);
            }
            Narsese_setAtomValue(nar, (Atom) Narsese_AtomicTermIndex(nar, termname_ext), value, termname);
        }
        else
        if(!strncmp("*space ", line, strlen("*space ")))
        {
            int granularity;
            char termname[ATOMIC_TERM_LEN_MAX+1] = {0};
            termname[ATOMIC_TERM_LEN_MAX-1] = 0;
            sscanf(&line[strlen("*space ")], "%d %" STR(ATOMIC_TERM_LEN_MAX) "s", &granularity, (char*) &termname);
            assert(granularity >= 1 && granularity <= 1000, "Granularity out of bounds!");
            for(int i=0; i<granularity; i++)
            {
                char setval[NARSESE_LEN_MAX+1] = {0};
                double fval = ((double) i) / ((double) granularity);
                sprintf(setval, "*setvalue %f %d %s", fval, granularity, termname);
                Shell_ProcessInput(nar, setval);
            }
        }
        else
        if(!strncmp("*setopname ", line, strlen("*setopname ")))
        {
            assert(nar->concepts.itemsAmount == 0, "Operators can only be registered right after initialization / reset!");
            int opID;
            char opname[ATOMIC_TERM_LEN_MAX+1] = {0};
            opname[ATOMIC_TERM_LEN_MAX-1] = 0;
            sscanf(&line[strlen("*setopname ")], "%d %" STR(ATOMIC_TERM_LEN_MAX) "s", &opID, (char*) &opname);
            assert(opID >= 1 && opID <= OPERATIONS_MAX, "Operator index out of bounds, it can only be between 1 and OPERATIONS_MAX!");
            Term newTerm = Narsese_AtomicTerm(nar, opname);
            for(int i=0; i<OPERATIONS_MAX; i++)
            {
                if(Term_Equal(&nar->operations[i].term, &newTerm)) //already exists, so clear the duplicate name
                {
                    for(int k=i; k<OPERATIONS_MAX; k++) //and the names of all the operations after it
                    {
                        nar->operations[k].term = (Term) {0};
                    }
                }
            }
            nar->operations[opID - 1].term = newTerm;
            if(!nar->operations[opID - 1].action) //allows to use more ops than are registered by the C code to be utilized through NAR.py
            {
                nar->operations[opID - 1].action = Shell_op_nop;
            }
        }
        else
        if(!strncmp("*setoparg ", line, strlen("*setoparg ")))
        {
            int opID;
            int opArgID;
            char argname[NARSESE_LEN_MAX+1] = {0};
            argname[NARSESE_LEN_MAX-1] = 0;
            sscanf(&line[strlen("*setoparg ")], "%d %d %" STR(NARSESE_LEN_MAX) "[^\n]", &opID, &opArgID, (char*) &argname);
            assert(opID >= 1 && opID <= OPERATIONS_MAX, "Operator index out of bounds, it can only be between 1 and OPERATIONS_MAX!");
            assert(opArgID >= 1 && opArgID <= OPERATIONS_BABBLE_ARGS_MAX, "Operator arg index out of bounds, it can only be between 1 and OPERATIONS_BABBLE_ARGS_MAX!");
            nar->operations[opID - 1].arguments[opArgID-1] = Narsese_Term(nar, argname);
        }
        else
        if(!strncmp("*query ", line, strlen("*query ")))
        {
            double threshold;
            char narsese[NARSESE_LEN_MAX+1] = {0};
            narsese[NARSESE_LEN_MAX-1] = 0;
            sscanf(&line[strlen("*query ")], "%lf %" STR(NARSESE_LEN_MAX) "[^\n]", &threshold, (char*) &narsese);
            assert(threshold >= 0.0 && threshold <= 1.0, "Query truth exp out of bounds!");
            if(NAR_AddInputNarsese2(nar, narsese, true, threshold) != NAR_OK)
            {
                fputs("//Error: query parse failed\n", stderr);
            }
        }
        else
        if(!strncmp("*setopstdin ", line, strlen("*setopstdin ")))
        {
            int opID;
            sscanf(&line[strlen("*setopstdin ")], "%d", &opID);
            nar->operations[opID - 1].stdinOutput = true;
        }
        else
        if(strspn(line, "0123456789") && strlen(line) == strspn(line, "0123456789"))
        {
            unsigned int steps;
            sscanf(line, "%u", &steps);
            printf("performing %u inference steps:\n", steps); fflush(stdout);
            NAR_Cycles(nar, steps);
            printf("done with %u additional inference steps.\n", steps); fflush(stdout);
        }
        else
        if(!strncmp("*concurrent", line, strlen("*concurrent")))
        {
            nar->currentTime -= 1;
        }
        else
        {
            if(NAR_AddInputNarsese(nar, line) != NAR_OK)
            {
                fputs("//Error: input parse failed\n", stderr);
            }
        }
    }
    fflush(stdout);
    return SHELL_CONTINUE;
}

void Shell_Start(NAR_t *nar)
{
    Shell_NARInit(nar);
    for(;;)
    {
        char *line = Linedit_Read("driftnars> ");
        if(line == NULL)
        {
            if(EXIT_STATS)
            {
                Stats_Print(nar, nar->currentTime);
            }
            break;
        }
        int cmd = Shell_ProcessInput(nar, line);
        if(cmd == SHELL_RESET) //reset?
        {
            Shell_NARInit(nar);
        }
        else
        if(cmd == SHELL_EXIT)
        {
            break;
        }
    }
    Linedit_Cleanup();
}
