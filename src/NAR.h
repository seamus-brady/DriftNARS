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

#ifndef H_NAR
#define H_NAR

//////////////////////////////////////
//  NAR - The main reasoner module  //
//////////////////////////////////////
//The API to send events to the reasoner
//and to register operations it can invoke
//plus initialization and execution of inference steps

//References//
//-----------//
#include <stdlib.h>
#include "Cycle.h"
#include "Narsese.h"
#include "Config.h"

//Defaults//
//---------//
#define NAR_DEFAULT_TRUTH ((Truth) { .frequency = NAR_DEFAULT_FREQUENCY, .confidence = NAR_DEFAULT_CONFIDENCE })

///////////////////////////////////////////////////////////////////////////////
//  NAR_t — full per-instance context (all mutable global state lives here) //
///////////////////////////////////////////////////////////////////////////////
struct NAR_t {

    /* ── Memory (Memory.c) ─────────────────────────────── */
    Concept      concept_storage[CONCEPTS_MAX];
    Item         concept_items_storage[CONCEPTS_MAX];
    PriorityQueue concepts;
    HashTable    HTconcepts;
    VMItem       HTconcepts_storage[CONCEPTS_MAX];
    VMItem      *HTconcepts_storageptrs[CONCEPTS_MAX];
    VMItem      *HTconcepts_HT[CONCEPTS_HASHTABLE_BUCKETS];

    Event        cycling_belief_event_storage[CYCLING_BELIEF_EVENTS_MAX];
    Item         cycling_belief_event_items_storage[CYCLING_BELIEF_EVENTS_MAX];
    PriorityQueue cycling_belief_events;

    Event        cycling_goal_event_storage[CYCLING_GOAL_EVENTS_LAYERS][CYCLING_GOAL_EVENTS_MAX];
    Item         cycling_goal_event_items_storage[CYCLING_GOAL_EVENTS_LAYERS][CYCLING_GOAL_EVENTS_MAX];
    PriorityQueue cycling_goal_events[CYCLING_GOAL_EVENTS_LAYERS];

    OccurrenceTimeIndex occurrenceTimeIndex;
    Operation    operations[OPERATIONS_MAX];
    int          concept_id;
    double       conceptPriorityThreshold;

    Event        selectedBeliefs[BELIEF_EVENT_SELECTIONS];
    double       selectedBeliefsPriority[BELIEF_EVENT_SELECTIONS];
    int          beliefsSelectedCnt;
    Event        selectedGoals[GOAL_EVENT_SELECTIONS];
    double       selectedGoalsPriority[GOAL_EVENT_SELECTIONS];
    int          goalsSelectedCnt;

    Term         term_restriction; /* Memory_RestrictDerivationsTo static */

    /* ── Runtime-tunable output flags ─────────────────── */
    bool         PRINT_DERIVATIONS;
    bool         PRINT_INPUT;
    double       PRINT_EVENTS_PRIORITY_THRESHOLD;
    bool         RESTRICTED_CONCEPT_CREATION;

    /* ── Narsese (Narsese.c) ───────────────────────────── */
    char         atom_names[ATOMS_MAX][ATOMIC_TERM_LEN_MAX];
    double       atom_values[ATOMS_MAX];
    bool         atom_has_value[ATOMS_MAX];
    char         atom_measurement_names[ATOMS_MAX][ATOMIC_TERM_LEN_MAX];
    Atom         SELF;
    HashTable    HTatoms;
    VMItem       HTatoms_storage[ATOMS_MAX];
    VMItem      *HTatoms_storageptrs[ATOMS_MAX];
    VMItem      *HTatoms_HT[ATOMS_HASHTABLE_BUCKETS];
    int          term_index;
    /* Parser scratch buffers */
#define NARSESE_REPLACE_LEN (3*NARSESE_LEN_MAX)
#define NARSESE_EXPAND_LEN  (9*NARSESE_LEN_MAX)
    char         parse_buf_replaced[NARSESE_REPLACE_LEN];
    char         parse_buf_expanded[NARSESE_EXPAND_LEN];
    char        *parse_tokens[NARSESE_LEN_MAX + 1];

    /* ── InvertedAtomIndex (InvertedAtomIndex.c) ───────── */
    ConceptChainElement  cce_storage[UNIFICATION_DEPTH * CONCEPTS_MAX];
    ConceptChainElement *cce_storageptrs[UNIFICATION_DEPTH * CONCEPTS_MAX];
    Stack                cce_stack;
    ConceptChainElement *inverted_atom_index[ATOMS_MAX];

    /* ── Decision (Decision.c) ─────────────────────────── */
    double       CONDITION_THRESHOLD;
    double       DECISION_THRESHOLD;
    double       ANTICIPATION_THRESHOLD;
    double       ANTICIPATION_CONFIDENCE;
    double       MOTOR_BABBLING_CHANCE;
    int          BABBLING_OPS;

    /* ── Truth (Truth.c) ───────────────────────────────── */
    double       TRUTH_EVIDENTIAL_HORIZON;
    double       TRUTH_PROJECTION_DECAY;

    /* ── Variable (Variable.c) ─────────────────────────── */
    double       similarity_distance;

    /* ── Event (Event.c) ───────────────────────────────── */
    long         stamp_base;
    Stamp        import_stamp;

    /* ── NAR core (NAR.c) ──────────────────────────────── */
    long         currentTime;
    double       QUESTION_PRIMING;
    int          op_k;
    bool         initialized;

    /* ── Cycle (Cycle.c) ───────────────────────────────── */
    long         conceptProcessID;
    long         conceptProcessID2;
    long         conceptProcessID3;

    /* ── RNG (Globals.c) ────────────────────────────────── */
    unsigned long rand_seed;

    /* ── Stats (Stats.c) ───────────────────────────────── */
    long         stats_concepts_matched_total;
    long         stats_concepts_matched_max;

    /* ── NAL (NAL.c) ────────────────────────────────────── */
    int          nal_ruleID;
    int          nal_atomsCounter;
    int          nal_atomsAppeared[ATOMS_MAX];
};

//Lifecycle//
//----------//
NAR_t *NAR_New(void);   // malloc + zero-init (returns NULL on failure)
void   NAR_Free(NAR_t *nar);

//Methods//
//-------//
//Init/Reset system; returns NAR_OK
int  NAR_INIT(NAR_t *nar);
//Run the system for a certain amount of cycles
void NAR_Cycles(NAR_t *nar, int cycles);
//Add input
Event NAR_AddInput(NAR_t *nar, Term term, char type, Truth truth, bool eternal, double occurrenceTimeOffset);
Event NAR_AddInputBelief(NAR_t *nar, Term term);
Event NAR_AddInputGoal(NAR_t *nar, Term term);
//Add an operation (asserts on programmer errors: null procedure, too many ops)
void NAR_AddOperation(NAR_t *nar, char *operator_name, Action procedure);
//Add a Narsese sentence; returns NAR_OK or NAR_ERR_PARSE
int  NAR_AddInputNarsese(NAR_t *nar, char *narsese_sentence);
//Add a Narsese sentence with query functionality; returns NAR_OK or NAR_ERR_PARSE
int  NAR_AddInputNarsese2(NAR_t *nar, char *narsese_sentence, bool queryCommand, double answerTruthExpThreshold);
#endif
