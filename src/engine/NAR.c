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

#include "NAR.h"
#include <stdio.h>
#include <stdint.h>

NAR_t *NAR_New(void)
{
    NAR_t *nar = (NAR_t *) calloc(1, sizeof(NAR_t));
    return nar;
}

void NAR_Free(NAR_t *nar)
{
    for(int i = 0; i < CONCEPTS_MAX; i++)
    {
        free(nar->concept_storage[i]); //free(NULL) is safe
    }
    free(nar);
}

int NAR_INIT(NAR_t *nar)
{
    assert(pow(TRUTH_PROJECTION_DECAY_INITIAL,EVENT_BELIEF_DISTANCE) >= MIN_CONFIDENCE, "Bad params, increase projection decay or decrease event belief distance!");
    /* ── Per-instance tunables ──────────────────────────── */
    nar->CONDITION_THRESHOLD  = CONDITION_THRESHOLD_INITIAL;
    nar->DECISION_THRESHOLD   = DECISION_THRESHOLD_INITIAL;
    nar->ANTICIPATION_THRESHOLD = ANTICIPATION_THRESHOLD_INITIAL;
    nar->ANTICIPATION_CONFIDENCE = ANTICIPATION_CONFIDENCE_INITIAL;
    nar->MOTOR_BABBLING_CHANCE = MOTOR_BABBLING_CHANCE_INITIAL;
    nar->BABBLING_OPS          = OPERATIONS_MAX;
    nar->QUESTION_PRIMING      = QUESTION_PRIMING_INITIAL;
    nar->similarity_distance   = SIMILARITY_DISTANCE;
    nar->TRUTH_EVIDENTIAL_HORIZON = TRUTH_EVIDENTIAL_HORIZON_INITIAL;
    nar->TRUTH_PROJECTION_DECAY   = TRUTH_PROJECTION_DECAY_INITIAL;
    /* ── Sync Truth.c externs (pragmatic: Truth.c uses globals) ── */
    TRUTH_EVIDENTIAL_HORIZON = nar->TRUTH_EVIDENTIAL_HORIZON;
    TRUTH_PROJECTION_DECAY   = nar->TRUTH_PROJECTION_DECAY;
    /* ── Core ───────────────────────────────────────────── */
    Memory_INIT(nar);
    Event_INIT(nar);
    Narsese_INIT(nar);
    Cycle_INIT(nar);
    nar->currentTime = 1;
    nar->initialized = true;
    nar->op_k = 0;
    nar->nal_atomsCounter = 1;
    return NAR_OK;
}

void NAR_Cycles(NAR_t *nar, int cycles)
{
    assert(nar->initialized, "NAR not initialized yet, call NAR_INIT first!");
    for(int i=0; i<cycles; i++)
    {
        IN_DEBUG( puts("\nNew system cycle:\n----------"); )
        Cycle_Perform(nar, nar->currentTime);
        nar->currentTime++;
    }
}

Event NAR_AddInput(NAR_t *nar, Term term, char type, Truth truth, bool eternal, double occurrenceTimeOffset)
{
    assert(nar->initialized, "NAR not initialized yet, call NAR_INIT first!");
    Event ev = Event_InputEvent(nar, term, type, truth, occurrenceTimeOffset, nar->currentTime);
    if(eternal)
    {
        ev.occurrenceTime = OCCURRENCE_ETERNAL;
    }
    Memory_AddInputEvent(nar, &ev, nar->currentTime);
    NAR_Cycles(nar, 1);
    return ev;
}

Event NAR_AddInputBelief(NAR_t *nar, Term term)
{
    return NAR_AddInput(nar, term, EVENT_TYPE_BELIEF, NAR_DEFAULT_TRUTH, false, 0);
}

Event NAR_AddInputGoal(NAR_t *nar, Term term)
{
    return NAR_AddInput(nar, term, EVENT_TYPE_GOAL, NAR_DEFAULT_TRUTH, false, 0);
}

void NAR_AddOperation(NAR_t *nar, char *operator_name, Action procedure)
{
    assert(procedure != 0, "Cannot add an operation with null-procedure");
    assert(nar->initialized, "NAR not initialized yet, call NAR_INIT first!");
    Term term = Narsese_AtomicTerm(nar, operator_name);
    assert(operator_name[0] == '^', "This atom does not belong to an operator!");
    //check if term already exists
    int existing_k = Memory_getOperationID(nar, &term);
    //use the running k if not existing yet
    int use_k = existing_k == 0 ? nar->op_k+1 : existing_k;
    //if it wasn't existing, also increase the running k and check if it's still in bounds
    assert(use_k <= OPERATIONS_MAX, "Too many operators, increase OPERATIONS_MAX!");
    if(existing_k == 0)
    {
        nar->op_k++;
    }
    nar->operations[use_k-1] = (Operation) { .term = term, .action = procedure };
}

static void NAR_PrintAnswer(NAR_t *nar, Stamp stamp, Term best_term, Truth best_truth, long answerOccurrenceTime, long answerCreationTime)
{
    if(nar->answer_handler)
    {
        if(best_truth.confidence == 1.1)
        {
            nar->answer_handler(nar->answer_handler_userdata, "None", 0.0, 0.0, answerOccurrenceTime, answerCreationTime);
        }
        else
        {
            char buf[NARSESE_SPRINT_BUFSIZE];
            Narsese_SprintTerm(nar, &best_term, buf, sizeof(buf));
            nar->answer_handler(nar->answer_handler_userdata, buf,
                best_truth.frequency, best_truth.confidence, answerOccurrenceTime, answerCreationTime);
        }
    }
    fputs("Answer: ", stdout);
    if(best_truth.confidence == 1.1)
    {
        puts("None.");
    }
    else
    {
        Narsese_PrintTerm(nar, &best_term);
        if(answerOccurrenceTime == OCCURRENCE_ETERNAL)
        {
            printf(". creationTime=%ld ", answerCreationTime);
        }
        else
        {
            printf(". :|: occurrenceTime=%ld creationTime=%ld ", answerOccurrenceTime, answerCreationTime);
        }
        Stamp_print(&stamp);
        fputs(" ", stdout);
        Truth_Print(&best_truth);
    }
    fflush(stdout);
}

int NAR_AddInputNarsese2(NAR_t *nar, char *narsese_sentence, bool queryCommand, double answerTruthExpThreshold)
{
    Term term;
    Truth tv;
    char punctuation;
    int tense;
    double occurrenceTimeOffset;
    int parse_result = Narsese_Sentence(nar, narsese_sentence, &term, &punctuation, &tense, &tv, &occurrenceTimeOffset);
    if(parse_result != NAR_OK)
    {
        return parse_result;
    }
#if STAGE==2
    //apply reduction rules to term:
    term = RuleTable_Reduce(nar, term);
#endif
    if(punctuation == '?')
    {
        //answer questions:
        Truth best_truth = { .frequency = 0.0, .confidence = 1.1 };
        Truth best_truth_projected = { .frequency = 0.0, .confidence = 1.0 };
        Concept* best_belief_concept = NULL;
        Term best_term = {0};
        Stamp best_stamp = {0};
        long answerOccurrenceTime = OCCURRENCE_ETERNAL;
        long answerCreationTime = 0;
        bool isImplication = Narsese_copulaEquals(nar, term.atoms[0], TEMPORAL_IMPLICATION);
        fputs("Input: ", stdout);
        Narsese_PrintTerm(nar, &term);
        fputs("?", stdout);
        Memory_Conceptualize(nar, &term, nar->currentTime);
        puts(tense == 1 ? " :|:" : (tense == 2 ? " :\\:" : (tense == 3 ? " :/:" : "")));
        fflush(stdout);
        for(int i=0; i<nar->concepts.itemsAmount; i++)
        {
            Concept *c = nar->concepts.items[i].address;
            //compare the predicate of implication, or if it's not an implication, the term
            Term toCompare = isImplication ? Term_ExtractSubterm(&term, 2) : term;
            Truth unused = (Truth) {0};
            if(Narsese_copulaEquals(nar, term.atoms[0], IMPLICATION))
            {
                Term toCompare2 = Term_ExtractSubterm(&term, 2);
                if(!Variable_Unify2(nar, unused, &toCompare2, &c->term, true).success)
                {
                    goto Continue;
                }
                for(int j=0; j<c->implication_links.itemsAmount; j++)
                {
                    Implication *imp = &c->implication_links.array[j];
                    if(!Variable_Unify2(nar, unused, &term, &imp->term, true).success)
                    {
                        continue;
                    }
                    if(queryCommand && Truth_Expectation(imp->truth) > answerTruthExpThreshold)
                    {
                        NAR_PrintAnswer(nar, imp->stamp, imp->term, imp->truth, answerOccurrenceTime, imp->creationTime);
                    }
                    if(Truth_Expectation(imp->truth) >= Truth_Expectation(best_truth))
                    {
                        best_stamp = imp->stamp;
                        best_truth = imp->truth;
                        best_term = imp->term;
                        answerCreationTime = imp->creationTime;
                    }
                }
            }
            if(!Variable_Unify2(nar, unused, &toCompare, &c->term, true).success)
            {
                goto Continue;
            }
            if(isImplication)
            {
                for(int op_k = 0; op_k<OPERATIONS_MAX; op_k++)
                {
                    for(int j=0; j<c->precondition_beliefs[op_k].itemsAmount; j++)
                    {
                        Implication *imp = &c->precondition_beliefs[op_k].array[j];
                        if(!Variable_Unify2(nar, unused, &term, &imp->term, true).success)
                        {
                            continue;
                        }
                        if(queryCommand && Truth_Expectation(imp->truth) > answerTruthExpThreshold)
                        {
                            NAR_PrintAnswer(nar, imp->stamp, imp->term, imp->truth, answerOccurrenceTime, imp->creationTime);
                        }
                        if(Truth_Expectation(imp->truth) >= Truth_Expectation(best_truth))
                        {
                            best_stamp = imp->stamp;
                            best_truth = imp->truth;
                            best_term = imp->term;
                            answerCreationTime = imp->creationTime;
                        }
                    }
                }
            }
            else
            if(tense)
            {
                if(c->belief_spike.type != EVENT_TYPE_DELETED && (tense == 1 || tense == 2))
                {
                    Truth potential_best_truth = Truth_Projection(c->belief_spike.truth, c->belief_spike.occurrenceTime, nar->currentTime);
                    if(queryCommand && Truth_Expectation(potential_best_truth) > answerTruthExpThreshold)
                    {
                        NAR_PrintAnswer(nar, c->belief_spike.stamp, c->belief_spike.term, c->belief_spike.truth, c->belief_spike.occurrenceTime, c->belief_spike.creationTime);
                    }
                    if( Truth_Expectation(potential_best_truth) >  Truth_Expectation(best_truth_projected) ||
                        (Truth_Expectation(c->belief_spike.truth) > Truth_Expectation(best_truth) && c->belief_spike.occurrenceTime == answerOccurrenceTime) ||
                       (Truth_Expectation(potential_best_truth) == Truth_Expectation(best_truth_projected) && c->belief_spike.occurrenceTime > answerOccurrenceTime))
                    {
                        best_stamp = c->belief_spike.stamp;
                        best_truth_projected = potential_best_truth;
                        best_truth = c->belief_spike.truth;
                        best_term = c->belief_spike.term;
                        best_belief_concept = c;
                        answerOccurrenceTime = c->belief_spike.occurrenceTime;
                        answerCreationTime = c->belief_spike.creationTime;
                    }
                }
                if(c->predicted_belief.type != EVENT_TYPE_DELETED && (tense == 1 || tense == 3))
                {
                    Truth potential_best_truth = Truth_Projection(c->predicted_belief.truth, c->predicted_belief.occurrenceTime, nar->currentTime);
                    if(queryCommand && Truth_Expectation(potential_best_truth) > answerTruthExpThreshold)
                    {
                        NAR_PrintAnswer(nar, c->predicted_belief.stamp, c->predicted_belief.term, c->predicted_belief.truth, c->predicted_belief.occurrenceTime, c->predicted_belief.creationTime);
                    }
                    if( Truth_Expectation(potential_best_truth) >  Truth_Expectation(best_truth_projected) ||
                       (Truth_Expectation(potential_best_truth) == Truth_Expectation(best_truth_projected) && c->predicted_belief.occurrenceTime > answerOccurrenceTime))
                    {
                        best_stamp = c->predicted_belief.stamp;
                        best_truth_projected = potential_best_truth;
                        best_truth = c->predicted_belief.truth;
                        best_term = c->predicted_belief.term;
                        best_belief_concept = c;
                        answerOccurrenceTime = c->predicted_belief.occurrenceTime;
                        answerCreationTime = c->predicted_belief.creationTime;
                    }
                }
            }
            else
            {
                if(c->belief.type != EVENT_TYPE_DELETED && queryCommand && Truth_Expectation(c->belief.truth) > answerTruthExpThreshold)
                {
                    NAR_PrintAnswer(nar, c->belief.stamp, c->belief.term, c->belief.truth, c->belief.occurrenceTime, c->belief.creationTime);
                }
                if(c->belief.type != EVENT_TYPE_DELETED && Truth_Expectation(c->belief.truth) >= Truth_Expectation(best_truth))
                {
                    best_stamp = c->belief.stamp;
                    best_truth = c->belief.truth;
                    best_term = c->belief.term;
                    best_belief_concept = c;
                    answerCreationTime = c->belief.creationTime;
                }
            }
            Continue:;
        }
        //simplistic priming for Q&A:
        if(best_belief_concept != NULL && nar->QUESTION_PRIMING > 0.0)
        {
            best_belief_concept->priority = MAX(best_belief_concept->priority, nar->QUESTION_PRIMING);
            best_belief_concept->usage = Usage_use(best_belief_concept->usage, nar->currentTime, tense == 0 ? true : false);
        }
        if(!queryCommand)
        {
            NAR_PrintAnswer(nar, best_stamp, best_term, best_truth, answerOccurrenceTime, answerCreationTime);
        }
    }
    //input beliefs and goals
    else
    {
        // dont add the input if it is an eternal goal
        if(punctuation == '!' && !tense)
        {
            fputs("Parsing error: Eternal goals are not supported!\n", stderr);
            return NAR_ERR_PARSE;
        }
        if(punctuation == '.' && tense >= 2)
        {
            fputs("Parsing error: Future and past belief events are not supported!\n", stderr);
            return NAR_ERR_PARSE;
        }
        NAR_AddInput(nar, term, punctuation == '!' ? EVENT_TYPE_GOAL : EVENT_TYPE_BELIEF, tv, !tense, occurrenceTimeOffset);
    }
    return NAR_OK;
}

int NAR_AddInputNarsese(NAR_t *nar, char *narsese_sentence)
{
    return NAR_AddInputNarsese2(nar, narsese_sentence, false, 0.0);
}

void NAR_SetEventHandler(NAR_t *nar, NAR_EventHandler handler, void *userdata)
{
    nar->event_handler = handler;
    nar->event_handler_userdata = userdata;
}

void NAR_SetAnswerHandler(NAR_t *nar, NAR_AnswerHandler handler, void *userdata)
{
    nar->answer_handler = handler;
    nar->answer_handler_userdata = userdata;
}

void NAR_SetDecisionHandler(NAR_t *nar, NAR_DecisionHandler handler, void *userdata)
{
    nar->decision_handler = handler;
    nar->decision_handler_userdata = userdata;
}

void NAR_SetExecutionHandler(NAR_t *nar, NAR_ExecutionHandler handler, void *userdata)
{
    nar->execution_handler = handler;
    nar->execution_handler_userdata = userdata;
}

static Feedback NAR_nop_action(Term args)
{
    (void)args;
    return (Feedback) {0};
}

void NAR_AddOperationName(NAR_t *nar, const char *op_name)
{
    NAR_AddOperation(nar, (char *)op_name, NAR_nop_action);
}

/* ── Compaction ───────────────────────────────────────────────────────────── */

int NAR_Compact(NAR_t *nar, int target_count)
{
    assert(nar->initialized, "NAR not initialized yet, call NAR_INIT first!");
    if(target_count < 0) target_count = 0;
    /* Collect pointers to concepts being freed so we can clean up references */
    int freed_count = 0;
    void *freed_ptrs[CONCEPTS_MAX];
    while(nar->concepts.itemsAmount > target_count)
    {
        void *addr = NULL;
        PriorityQueue_PopMin(&nar->concepts, &addr, NULL);
        if(!addr) break;
        Concept *c = (Concept *)addr;
        /* Remove from hashtable and inverted atom index */
        HashTable_Delete(&nar->HTconcepts, &c->term);
        InvertedAtomIndex_RemoveConcept(nar, c->term, c);
        /* Free the concept */
        nar->concept_storage[c->storage_index] = NULL;
        nar->concepts_allocated--;
        freed_ptrs[freed_count++] = c;
        free(c);
        /* Clear the PQ slot so lazy allocation works on next push */
        nar->concepts.items[nar->concepts.itemsAmount].address = NULL;
    }
    /* Null out dangling Implication.sourceConcept pointers in surviving concepts */
    for(int i = 0; i < nar->concepts.itemsAmount; i++)
    {
        Concept *c = nar->concepts.items[i].address;
        for(int opi = 0; opi <= OPERATIONS_MAX; opi++)
        {
            for(int j = 0; j < c->precondition_beliefs[opi].itemsAmount; j++)
            {
                Implication *imp = &c->precondition_beliefs[opi].array[j];
                for(int k = 0; k < freed_count; k++)
                {
                    if(imp->sourceConcept == freed_ptrs[k])
                    {
                        imp->sourceConcept = NULL;
                        break;
                    }
                }
            }
        }
        for(int j = 0; j < c->implication_links.itemsAmount; j++)
        {
            Implication *imp = &c->implication_links.array[j];
            for(int k = 0; k < freed_count; k++)
            {
                if(imp->sourceConcept == freed_ptrs[k])
                {
                    imp->sourceConcept = NULL;
                    break;
                }
            }
        }
    }
    /* Reset occurrence time index — temporal context is stale after compaction */
    nar->occurrenceTimeIndex = (OccurrenceTimeIndex) {0};
    return nar->concepts.itemsAmount;
}

/* ── State serialization ──────────────────────────────────────────────────── */

#define DNAR_MAGIC   0x444E5253U  /* "DNRS" */
#define DNAR_VERSION 2

#define W(ptr, sz) do { if(fwrite((ptr), (sz), 1, f) != 1) goto fail; } while(0)
#define WV(val)    W(&(val), sizeof(val))

int NAR_Save(NAR_t *nar, const char *path)
{
    assert(nar->initialized, "NAR not initialized yet, call NAR_INIT first!");
    FILE *f = fopen(path, "wb");
    if(!f) return NAR_ERR_IO;

    /* ── Header ──────────────────────────────────────────── */
    uint32_t magic = DNAR_MAGIC, version = DNAR_VERSION;
    WV(magic); WV(version);
    int cfg[] = { CONCEPTS_MAX, ATOMS_MAX, OPERATIONS_MAX,
                  CYCLING_BELIEF_EVENTS_MAX, CYCLING_GOAL_EVENTS_MAX,
                  CYCLING_GOAL_EVENTS_LAYERS, COMPOUND_TERM_SIZE_MAX,
                  STAMP_SIZE, TABLE_SIZE, ATOMIC_TERM_LEN_MAX,
                  OPERATIONS_BABBLE_ARGS_MAX, OCCURRENCE_TIME_INDEX_SIZE };
    W(cfg, sizeof(cfg));

    /* ── Scalars ─────────────────────────────────────────── */
    WV(nar->currentTime);
    WV(nar->concept_id);
    WV(nar->term_index);
    WV(nar->stamp_base);
    WV(nar->import_stamp);
    WV(nar->conceptProcessID);
    WV(nar->conceptProcessID2);
    WV(nar->conceptProcessID3);
    WV(nar->rand_seed);
    WV(nar->stats_concepts_matched_total);
    WV(nar->stats_concepts_matched_max);
    WV(nar->op_k);
    WV(nar->nal_ruleID);
    WV(nar->nal_atomsCounter);
    W(nar->nal_atomsAppeared, sizeof(nar->nal_atomsAppeared));
    WV(nar->CONDITION_THRESHOLD);
    WV(nar->DECISION_THRESHOLD);
    WV(nar->ANTICIPATION_THRESHOLD);
    WV(nar->ANTICIPATION_CONFIDENCE);
    WV(nar->MOTOR_BABBLING_CHANCE);
    WV(nar->BABBLING_OPS);
    WV(nar->TRUTH_EVIDENTIAL_HORIZON);
    WV(nar->TRUTH_PROJECTION_DECAY);
    WV(nar->similarity_distance);
    WV(nar->QUESTION_PRIMING);
    WV(nar->PRINT_DERIVATIONS);
    WV(nar->PRINT_INPUT);
    WV(nar->PRINT_EVENTS_PRIORITY_THRESHOLD);
    WV(nar->RESTRICTED_CONCEPT_CREATION);
    WV(nar->conceptPriorityThreshold);
    WV(nar->SELF);
    WV(nar->term_restriction);

    /* ── Atoms ───────────────────────────────────────────── */
    W(nar->atom_names, sizeof(nar->atom_names));
    W(nar->atom_values, sizeof(nar->atom_values));
    W(nar->atom_has_value, sizeof(nar->atom_has_value));
    W(nar->atom_measurement_names, sizeof(nar->atom_measurement_names));

    /* ── Concepts (dynamically allocated) ───────────────── */
    WV(nar->concepts_allocated);
    for(int i = 0; i < CONCEPTS_MAX; i++)
    {
        bool allocated = (nar->concept_storage[i] != NULL);
        WV(allocated);
        if(allocated)
        {
            W(nar->concept_storage[i], sizeof(Concept));
        }
    }
    WV(nar->concepts.itemsAmount);
    for(int i = 0; i < nar->concepts.itemsAmount; i++)
    {
        WV(nar->concepts.items[i].priority);
        int idx = ((Concept *)nar->concepts.items[i].address)->storage_index;
        WV(idx);
    }

    /* ── Belief events ───────────────────────────────────── */
    W(nar->cycling_belief_event_storage, sizeof(nar->cycling_belief_event_storage));
    WV(nar->cycling_belief_events.itemsAmount);
    for(int i = 0; i < nar->cycling_belief_events.itemsAmount; i++)
    {
        WV(nar->cycling_belief_events.items[i].priority);
        int idx = (int)((Event *)nar->cycling_belief_events.items[i].address - nar->cycling_belief_event_storage);
        WV(idx);
    }

    /* ── Goal events (per layer) ─────────────────────────── */
    for(int layer = 0; layer < CYCLING_GOAL_EVENTS_LAYERS; layer++)
    {
        W(nar->cycling_goal_event_storage[layer], sizeof(nar->cycling_goal_event_storage[layer]));
        WV(nar->cycling_goal_events[layer].itemsAmount);
        for(int i = 0; i < nar->cycling_goal_events[layer].itemsAmount; i++)
        {
            WV(nar->cycling_goal_events[layer].items[i].priority);
            int idx = (int)((Event *)nar->cycling_goal_events[layer].items[i].address - nar->cycling_goal_event_storage[layer]);
            WV(idx);
        }
    }

    /* ── OccurrenceTimeIndex ─────────────────────────────── */
    WV(nar->occurrenceTimeIndex.itemsAmount);
    WV(nar->occurrenceTimeIndex.currentIndex);
    for(int i = 0; i < OCCURRENCE_TIME_INDEX_SIZE; i++)
    {
        int idx = nar->occurrenceTimeIndex.array[i]
                ? nar->occurrenceTimeIndex.array[i]->storage_index
                : -1;
        WV(idx);
    }

    /* ── Operations (term + args, no function pointers) ──── */
    for(int i = 0; i < OPERATIONS_MAX; i++)
    {
        WV(nar->operations[i].term);
        W(nar->operations[i].arguments, sizeof(nar->operations[i].arguments));
        WV(nar->operations[i].stdinOutput);
    }

    fclose(f);
    return NAR_OK;
fail:
    fclose(f);
    return NAR_ERR_IO;
}

#undef W
#undef WV

#define R(ptr, sz) do { if(fread((ptr), (sz), 1, f) != 1) goto fail; } while(0)
#define RV(val)    R(&(val), sizeof(val))

int NAR_Load(NAR_t *nar, const char *path)
{
    FILE *f = fopen(path, "rb");
    if(!f) return NAR_ERR_IO;

    /* ── Header ──────────────────────────────────────────── */
    uint32_t magic, version;
    RV(magic); RV(version);
    if(magic != DNAR_MAGIC || version != DNAR_VERSION) goto fail;
    int cfg[12];
    R(cfg, sizeof(cfg));
    if(cfg[0]  != CONCEPTS_MAX || cfg[1]  != ATOMS_MAX ||
       cfg[2]  != OPERATIONS_MAX || cfg[3]  != CYCLING_BELIEF_EVENTS_MAX ||
       cfg[4]  != CYCLING_GOAL_EVENTS_MAX || cfg[5]  != CYCLING_GOAL_EVENTS_LAYERS ||
       cfg[6]  != COMPOUND_TERM_SIZE_MAX || cfg[7]  != STAMP_SIZE ||
       cfg[8]  != TABLE_SIZE || cfg[9]  != ATOMIC_TERM_LEN_MAX ||
       cfg[10] != OPERATIONS_BABBLE_ARGS_MAX || cfg[11] != OCCURRENCE_TIME_INDEX_SIZE)
        goto fail;

    /* Preserve callbacks and operation actions across load */
    NAR_EventHandler     saved_eh  = nar->event_handler;
    void                *saved_ehu = nar->event_handler_userdata;
    NAR_AnswerHandler    saved_ah  = nar->answer_handler;
    void                *saved_ahu = nar->answer_handler_userdata;
    NAR_DecisionHandler  saved_dh  = nar->decision_handler;
    void                *saved_dhu = nar->decision_handler_userdata;
    NAR_ExecutionHandler saved_xh  = nar->execution_handler;
    void                *saved_xhu = nar->execution_handler_userdata;
    Action saved_actions[OPERATIONS_MAX];
    for(int i = 0; i < OPERATIONS_MAX; i++)
        saved_actions[i] = nar->operations[i].action;

    /* ── Scalars ─────────────────────────────────────────── */
    RV(nar->currentTime);
    RV(nar->concept_id);
    RV(nar->term_index);
    RV(nar->stamp_base);
    RV(nar->import_stamp);
    RV(nar->conceptProcessID);
    RV(nar->conceptProcessID2);
    RV(nar->conceptProcessID3);
    RV(nar->rand_seed);
    RV(nar->stats_concepts_matched_total);
    RV(nar->stats_concepts_matched_max);
    RV(nar->op_k);
    RV(nar->nal_ruleID);
    RV(nar->nal_atomsCounter);
    R(nar->nal_atomsAppeared, sizeof(nar->nal_atomsAppeared));
    RV(nar->CONDITION_THRESHOLD);
    RV(nar->DECISION_THRESHOLD);
    RV(nar->ANTICIPATION_THRESHOLD);
    RV(nar->ANTICIPATION_CONFIDENCE);
    RV(nar->MOTOR_BABBLING_CHANCE);
    RV(nar->BABBLING_OPS);
    RV(nar->TRUTH_EVIDENTIAL_HORIZON);
    RV(nar->TRUTH_PROJECTION_DECAY);
    RV(nar->similarity_distance);
    RV(nar->QUESTION_PRIMING);
    RV(nar->PRINT_DERIVATIONS);
    RV(nar->PRINT_INPUT);
    RV(nar->PRINT_EVENTS_PRIORITY_THRESHOLD);
    RV(nar->RESTRICTED_CONCEPT_CREATION);
    RV(nar->conceptPriorityThreshold);
    RV(nar->SELF);
    RV(nar->term_restriction);
    /* Sync Truth.c globals */
    TRUTH_EVIDENTIAL_HORIZON = nar->TRUTH_EVIDENTIAL_HORIZON;
    TRUTH_PROJECTION_DECAY   = nar->TRUTH_PROJECTION_DECAY;

    /* ── Atoms ───────────────────────────────────────────── */
    R(nar->atom_names, sizeof(nar->atom_names));
    R(nar->atom_values, sizeof(nar->atom_values));
    R(nar->atom_has_value, sizeof(nar->atom_has_value));
    R(nar->atom_measurement_names, sizeof(nar->atom_measurement_names));
    /* Rebuild atom hashtable */
    HashTable_INIT(&nar->HTatoms, nar->HTatoms_storage, nar->HTatoms_storageptrs,
                   nar->HTatoms_HT, ATOMS_HASHTABLE_BUCKETS, ATOMS_MAX,
                   (Equal) Narsese_StringEqual, (Hash) Narsese_StringHash);
    for(int i = 0; i < nar->term_index; i++)
    {
        HashTable_Set(&nar->HTatoms, (void *)nar->atom_names[i], (void *)(long)(i + 1));
    }

    /* ── Concepts (dynamically allocated) ───────────────── */
    /* Free any existing concepts first */
    for(int i = 0; i < CONCEPTS_MAX; i++)
    {
        free(nar->concept_storage[i]);
        nar->concept_storage[i] = NULL;
    }
    RV(nar->concepts_allocated);
    for(int i = 0; i < CONCEPTS_MAX; i++)
    {
        bool allocated;
        RV(allocated);
        if(allocated)
        {
            nar->concept_storage[i] = (Concept *) calloc(1, sizeof(Concept));
            if(!nar->concept_storage[i]) goto fail;
            R(nar->concept_storage[i], sizeof(Concept));
        }
    }
    /* Rebuild concept hashtable */
    HashTable_INIT(&nar->HTconcepts, nar->HTconcepts_storage, nar->HTconcepts_storageptrs,
                   nar->HTconcepts_HT, CONCEPTS_HASHTABLE_BUCKETS, CONCEPTS_MAX,
                   (Equal) Term_Equal, (Hash) Term_Hash);
    /* Rebuild inverted atom index */
    InvertedAtomIndex_INIT(nar);
    /* Rebuild concept PQ */
    PriorityQueue_INIT(&nar->concepts, nar->concept_items_storage, CONCEPTS_MAX);
    for(int i = 0; i < CONCEPTS_MAX; i++)
        nar->concepts.items[i].address = nar->concept_storage[i]; //may be NULL for unallocated slots
    RV(nar->concepts.itemsAmount);
    for(int i = 0; i < nar->concepts.itemsAmount; i++)
    {
        double priority;
        int idx;
        RV(priority); RV(idx);
        nar->concepts.items[i].priority = priority;
        nar->concepts.items[i].address = nar->concept_storage[idx];
    }
    PriorityQueue_Rebuild(&nar->concepts);
    /* Re-insert concepts into hashtable and inverted atom index */
    for(int i = 0; i < nar->concepts.itemsAmount; i++)
    {
        Concept *c = nar->concepts.items[i].address;
        if(c->term.atoms[0])
        {
            HashTable_Set(&nar->HTconcepts, &c->term, c);
            InvertedAtomIndex_AddConcept(nar, c->term, c);
        }
    }
    /* Remap Implication.sourceConcept pointers */
    for(int i = 0; i < nar->concepts.itemsAmount; i++)
    {
        Concept *c = nar->concepts.items[i].address;
        for(int opi = 0; opi <= OPERATIONS_MAX; opi++)
        {
            for(int j = 0; j < c->precondition_beliefs[opi].itemsAmount; j++)
            {
                Implication *imp = &c->precondition_beliefs[opi].array[j];
                imp->sourceConcept = NULL;
                for(int k = 0; k < nar->concepts.itemsAmount; k++)
                {
                    Concept *sc = nar->concepts.items[k].address;
                    if(sc->id == imp->sourceConceptId)
                    {
                        imp->sourceConcept = sc;
                        break;
                    }
                }
            }
        }
        for(int j = 0; j < c->implication_links.itemsAmount; j++)
        {
            Implication *imp = &c->implication_links.array[j];
            imp->sourceConcept = NULL;
            for(int k = 0; k < nar->concepts.itemsAmount; k++)
            {
                Concept *sc = nar->concepts.items[k].address;
                if(sc->id == imp->sourceConceptId)
                {
                    imp->sourceConcept = sc;
                    break;
                }
            }
        }
    }

    /* ── Belief events ───────────────────────────────────── */
    R(nar->cycling_belief_event_storage, sizeof(nar->cycling_belief_event_storage));
    PriorityQueue_INIT(&nar->cycling_belief_events, nar->cycling_belief_event_items_storage, CYCLING_BELIEF_EVENTS_MAX);
    for(int i = 0; i < CYCLING_BELIEF_EVENTS_MAX; i++)
        nar->cycling_belief_events.items[i].address = &nar->cycling_belief_event_storage[i];
    RV(nar->cycling_belief_events.itemsAmount);
    for(int i = 0; i < nar->cycling_belief_events.itemsAmount; i++)
    {
        double priority;
        int idx;
        RV(priority); RV(idx);
        nar->cycling_belief_events.items[i].priority = priority;
        nar->cycling_belief_events.items[i].address = &nar->cycling_belief_event_storage[idx];
    }
    PriorityQueue_Rebuild(&nar->cycling_belief_events);

    /* ── Goal events (per layer) ─────────────────────────── */
    for(int layer = 0; layer < CYCLING_GOAL_EVENTS_LAYERS; layer++)
    {
        R(nar->cycling_goal_event_storage[layer], sizeof(nar->cycling_goal_event_storage[layer]));
        PriorityQueue_INIT(&nar->cycling_goal_events[layer], nar->cycling_goal_event_items_storage[layer], CYCLING_GOAL_EVENTS_MAX);
        for(int i = 0; i < CYCLING_GOAL_EVENTS_MAX; i++)
            nar->cycling_goal_events[layer].items[i].address = &nar->cycling_goal_event_storage[layer][i];
        RV(nar->cycling_goal_events[layer].itemsAmount);
        for(int i = 0; i < nar->cycling_goal_events[layer].itemsAmount; i++)
        {
            double priority;
            int idx;
            RV(priority); RV(idx);
            nar->cycling_goal_events[layer].items[i].priority = priority;
            nar->cycling_goal_events[layer].items[i].address = &nar->cycling_goal_event_storage[layer][idx];
        }
        PriorityQueue_Rebuild(&nar->cycling_goal_events[layer]);
    }

    /* ── OccurrenceTimeIndex ─────────────────────────────── */
    RV(nar->occurrenceTimeIndex.itemsAmount);
    RV(nar->occurrenceTimeIndex.currentIndex);
    for(int i = 0; i < OCCURRENCE_TIME_INDEX_SIZE; i++)
    {
        int idx;
        RV(idx);
        nar->occurrenceTimeIndex.array[i] = (idx >= 0) ? nar->concept_storage[idx] : NULL;
    }

    /* ── Operations ──────────────────────────────────────── */
    for(int i = 0; i < OPERATIONS_MAX; i++)
    {
        RV(nar->operations[i].term);
        R(nar->operations[i].arguments, sizeof(nar->operations[i].arguments));
        RV(nar->operations[i].stdinOutput);
        nar->operations[i].action = saved_actions[i] ? saved_actions[i] : NAR_nop_action;
    }

    /* Restore callbacks */
    nar->event_handler = saved_eh;
    nar->event_handler_userdata = saved_ehu;
    nar->answer_handler = saved_ah;
    nar->answer_handler_userdata = saved_ahu;
    nar->decision_handler = saved_dh;
    nar->decision_handler_userdata = saved_dhu;
    nar->execution_handler = saved_xh;
    nar->execution_handler_userdata = saved_xhu;
    nar->initialized = true;

    fclose(f);
    return NAR_OK;
fail:
    fclose(f);
    return NAR_ERR_IO;
}

#undef R
#undef RV
