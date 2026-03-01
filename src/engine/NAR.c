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

NAR_t *NAR_New(void)
{
    NAR_t *nar = (NAR_t *) calloc(1, sizeof(NAR_t));
    return nar;
}

void NAR_Free(NAR_t *nar)
{
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
