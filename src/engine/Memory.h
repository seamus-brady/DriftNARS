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

#ifndef H_MEMORY
#define H_MEMORY

/////////////////
//  NAR Memory //
/////////////////
//The concept-based memory of NAR

//References//
//////////////
#include <math.h>
#include "Concept.h"
#include "OccurrenceTimeIndex.h"
#include "InvertedAtomIndex.h"
#include "PriorityQueue.h"
#include "Config.h"
#include "HashTable.h"
#include "Variable.h"

//Data structure//
//--------------//
typedef struct
{
    Substitution subs;
    bool failed;
}Feedback; //operation feedback
typedef Feedback (*Action)(Term);
typedef struct
{
    Term term;
    Action action;
    Term arguments[OPERATIONS_BABBLE_ARGS_MAX];
    bool stdinOutput;
}Operation;

//Methods//
//-------//
//Init memory
void Memory_INIT(NAR_t *nar);
//Find a concept
Concept *Memory_FindConceptByTerm(NAR_t *nar, Term *term);
//Create a new concept
Concept* Memory_Conceptualize(NAR_t *nar, Term *term, long currentTime);
//Add event to memory
void Memory_AddEvent(NAR_t *nar, Event *event, long currentTime, double priority, bool input, bool derived, bool revised, int layer, bool eternalize);
void Memory_AddInputEvent(NAR_t *nar, Event *event, long currentTime);
//check if implication is still valid (source concept might be forgotten)
bool Memory_ImplicationValid(Implication *imp);
//Print an event in memory:
void Memory_printAddedEvent(NAR_t *nar, Stamp *stamp, Event *event, double priority, bool input, bool derived, bool revised, bool controlInfo, bool selected);
//Print an implication in memory:
void Memory_printAddedImplication(NAR_t *nar, Stamp *stamp, Term *implication, Truth *truth, double occurrenceTimeOffset, double priority, bool input, bool revised, bool controlInfo);
//Get operation ID
int Memory_getOperationID(NAR_t *nar, Term *term);
//Get temporal link truth value
Truth Memory_getTemporalLinkTruth(NAR_t *nar, Term *precondition, Term *postcondition);
//Derivations restrictions from goal derivation
void Memory_RestrictDerivationsTo(NAR_t *nar, Term *term);
//Check if memory contains event
bool Memory_containsBeliefOrGoal(NAR_t *nar, Event *e);

#endif
