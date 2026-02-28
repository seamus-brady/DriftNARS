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

#include "Memory.h"
#include "NAR.h"

static void Memory_ResetEvents(NAR_t *nar)
{
    PriorityQueue_INIT(&nar->cycling_belief_events, nar->cycling_belief_event_items_storage, CYCLING_BELIEF_EVENTS_MAX);
    for(int i=0; i<CYCLING_BELIEF_EVENTS_MAX; i++)
    {
        nar->cycling_belief_event_storage[i] = (Event) {0};
        nar->cycling_belief_events.items[i] = (Item) { .address = &(nar->cycling_belief_event_storage[i]) };
    }
    for(int layer=0; layer<CYCLING_GOAL_EVENTS_LAYERS; layer++)
    {
        PriorityQueue_INIT(&nar->cycling_goal_events[layer], nar->cycling_goal_event_items_storage[layer], CYCLING_GOAL_EVENTS_MAX);
        for(int i=0; i<CYCLING_GOAL_EVENTS_MAX; i++)
        {
            nar->cycling_goal_event_storage[layer][i] = (Event) {0};
            nar->cycling_goal_events[layer].items[i] = (Item) { .address = &(nar->cycling_goal_event_storage[layer][i]) };
        }
    }
}

static void Memory_ResetConcepts(NAR_t *nar)
{
    PriorityQueue_INIT(&nar->concepts, nar->concept_items_storage, CONCEPTS_MAX);
    for(int i=0; i<CONCEPTS_MAX; i++)
    {
        nar->concept_storage[i] = (Concept) {0};
        nar->concepts.items[i] = (Item) { .address = &(nar->concept_storage[i]) };
    }
}

void Memory_INIT(NAR_t *nar)
{
    nar->RESTRICTED_CONCEPT_CREATION = RESTRICTED_CONCEPT_CREATION_INITIAL;
    nar->PRINT_DERIVATIONS = PRINT_DERIVATIONS_INITIAL;
    nar->PRINT_INPUT = PRINT_INPUT_INITIAL;
    nar->PRINT_EVENTS_PRIORITY_THRESHOLD = PRINT_EVENTS_PRIORITY_THRESHOLD_INITIAL;
    nar->conceptPriorityThreshold = 0.0;
    nar->concept_id = 0;
    HashTable_INIT(&nar->HTconcepts, nar->HTconcepts_storage, nar->HTconcepts_storageptrs, nar->HTconcepts_HT, CONCEPTS_HASHTABLE_BUCKETS, CONCEPTS_MAX, (Equal) Term_Equal, (Hash) Term_Hash);
    Memory_ResetConcepts(nar);
    Memory_ResetEvents(nar);
    InvertedAtomIndex_INIT(nar);
    nar->occurrenceTimeIndex = (OccurrenceTimeIndex) {0};
    for(int i=0; i<OPERATIONS_MAX; i++)
    {
        nar->operations[i] = (Operation) {0};
    }
}

Concept *Memory_FindConceptByTerm(NAR_t *nar, Term *term)
{
    return HashTable_Get(&nar->HTconcepts, term);
}

Concept* Memory_Conceptualize(NAR_t *nar, Term *term, long currentTime)
{
    Concept *ret = Memory_FindConceptByTerm(nar, term);
    if(ret == NULL)
    {
        Concept *recycleConcept = NULL;
        //try to add it, and if successful add to voting structure
        PriorityQueue_Push_Feedback feedback = PriorityQueue_Push(&nar->concepts, 1);
        if(feedback.added)
        {
            recycleConcept = feedback.addedItem.address;
            //if something was evicted in the adding process delete from hashmap first
            if(feedback.evicted)
            {
                IN_DEBUG( assert(HashTable_Get(&nar->HTconcepts, &recycleConcept->term) != NULL, "VMItem to delete does not exist!"); )
                HashTable_Delete(&nar->HTconcepts, &recycleConcept->term);
                IN_DEBUG( assert(HashTable_Get(&nar->HTconcepts, &recycleConcept->term) == NULL, "VMItem to delete was not deleted!"); )
                //and also delete from inverted atom index:
                InvertedAtomIndex_RemoveConcept(nar, recycleConcept->term, recycleConcept);
            }
            //Add term to inverted atom index as well:
            InvertedAtomIndex_AddConcept(nar, *term, recycleConcept);
            //proceed with recycling of the concept in the priority queue
            *recycleConcept = (Concept) {0};
            recycleConcept->term = *term;
            recycleConcept->id = nar->concept_id;
            recycleConcept->usage = (Usage) { .useCount = 1, .lastUsed = currentTime };
            nar->concept_id++;
            //also add added concept to HashMap:
            IN_DEBUG( assert(HashTable_Get(&nar->HTconcepts, &recycleConcept->term) == NULL, "VMItem to add already exists!"); )
            HashTable_Set(&nar->HTconcepts, &recycleConcept->term, recycleConcept);
            IN_DEBUG( assert(HashTable_Get(&nar->HTconcepts, &recycleConcept->term) != NULL, "VMItem to add was not added!"); )
            return recycleConcept;
        }
    }
    else
    {
        return ret;
    }
    return NULL;
}

static bool Memory_containsEvent(PriorityQueue *queue, Event *event)
{
    for(int i=0; i<queue->itemsAmount; i++)
    {
        if(Event_Equal(event, queue->items[i].address))
        {
            return true;
        }
    }
    return false;
}

bool Memory_containsBeliefOrGoal(NAR_t *nar, Event *e)
{
    Concept *c = Memory_FindConceptByTerm(nar, &e->term);
    if(c != NULL)
    {
        if(e->type == EVENT_TYPE_BELIEF)
        {
            if(e->occurrenceTime == OCCURRENCE_ETERNAL)
            {
                if(c->belief.type != EVENT_TYPE_DELETED && Event_Equal(e, &c->belief))
                {
                    return true;
                }
            }
            else
            if(c->belief_spike.type != EVENT_TYPE_DELETED && Event_EqualTermEqualStampLessConfidentThan(e, &c->belief_spike))
            {
                return true;
            }
        }
        else
        if(e->type == EVENT_TYPE_GOAL && c->goal_spike.type != EVENT_TYPE_DELETED && Event_Equal(&c->goal_spike, e))
        {
            return true;
        }
    }
    return false;
}

//Add event for cycling through the system (inference and context)
//called by addEvent for eternal knowledge
bool Memory_addCyclingEvent(NAR_t *nar, Event *e, double priority, long currentTime, int layer)
{
    assert(e->type == EVENT_TYPE_BELIEF || e->type == EVENT_TYPE_GOAL, "Only belief and goals events can be added to cycling events queue!");
    if((e->type == EVENT_TYPE_BELIEF && Memory_containsEvent(&nar->cycling_belief_events, e)) || Memory_containsBeliefOrGoal(nar, e)) //avoid duplicate derivations
    {
        return false;
    }
    if(e->type == EVENT_TYPE_GOAL) //avoid duplicate derivations
    {
        for(int layer=0; layer<CYCLING_GOAL_EVENTS_LAYERS; layer++)
        {
            if(Memory_containsEvent(&nar->cycling_goal_events[layer], e))
            {
                return false;
            }
        }
    }
    Concept *c = Memory_FindConceptByTerm(nar, &e->term);
    if(c != NULL)
    {
        if(e->type == EVENT_TYPE_BELIEF && c->belief.type != EVENT_TYPE_DELETED && e->occurrenceTime == OCCURRENCE_ETERNAL && c->belief.truth.confidence > e->truth.confidence)
        {
            return false; //the belief has a higher confidence and was already revised up (or a cyclic transformation happened!), get rid of the event!
        }   //more radical than OpenNARS!
    }
    PriorityQueue *priority_queue = e->type == EVENT_TYPE_BELIEF ? &nar->cycling_belief_events : &nar->cycling_goal_events[MIN(CYCLING_GOAL_EVENTS_LAYERS-1, layer+1)];
    PriorityQueue_Push_Feedback feedback = PriorityQueue_Push(priority_queue, priority);
    if(feedback.added)
    {
        Event *toRecycle = feedback.addedItem.address;
        *toRecycle = *e;
        return true;
    }
    return false;
}

static void Memory_printAddedKnowledge(NAR_t *nar, Stamp *stamp, Term *term, char type, Truth *truth, long occurrenceTime, double occurrenceTimeOffset, double priority, bool input, bool derived, bool revised, bool controlInfo, bool selected)
{
    if((input && nar->PRINT_INPUT) || (!input && nar->PRINT_DERIVATIONS && priority > nar->PRINT_EVENTS_PRIORITY_THRESHOLD))
    {
        if(controlInfo)
            fputs(selected ? "Selected: " : (revised ? "Revised: " : (input ? "Input: " : "Derived: ")), stdout);
        if(Narsese_copulaEquals(nar, term->atoms[0], TEMPORAL_IMPLICATION))
            printf("dt=%f ", occurrenceTimeOffset);
        Narsese_PrintTerm(nar, term);
        fputs((type == EVENT_TYPE_BELIEF ? ". " : "! "), stdout);
        if(occurrenceTime != OCCURRENCE_ETERNAL)
        {
            printf(":|: occurrenceTime=%ld ", occurrenceTime);
        }
        if(controlInfo)
        {
            printf("Priority=%f ", priority);
            Stamp_print(stamp);
            fputs(" ", stdout);
            Truth_Print(truth);
        }
        else
        {
            Truth_Print2(truth);
        }
        fflush(stdout);
    }
}

void Memory_printAddedEvent(NAR_t *nar, Stamp *stamp, Event *event, double priority, bool input, bool derived, bool revised, bool controlInfo, bool selected)
{
    Memory_printAddedKnowledge(nar, stamp, &event->term, event->type, &event->truth, event->occurrenceTime, event->occurrenceTimeOffset, priority, input, derived, revised, controlInfo, selected);
}

void Memory_printAddedImplication(NAR_t *nar, Stamp *stamp, Term *implication, Truth *truth, double occurrenceTimeOffset, double priority, bool input, bool revised, bool controlInfo)
{
    Memory_printAddedKnowledge(nar, stamp, implication, EVENT_TYPE_BELIEF, truth, OCCURRENCE_ETERNAL, occurrenceTimeOffset, priority, input, true, revised, controlInfo, false);
}

static void Memory_ProcessNewBeliefEvent(NAR_t *nar, Event *event, long currentTime, double priority, bool input, bool eternalize)
{
    if(event->truth.confidence < MIN_CONFIDENCE)
    {
        return;
    }
    bool eternalInput = input && event->occurrenceTime == OCCURRENCE_ETERNAL;
    Event eternal_event = Event_Eternalized(event);
    if(eternalize && (Narsese_copulaEquals(nar, event->term.atoms[0], TEMPORAL_IMPLICATION) || Narsese_copulaEquals(nar, event->term.atoms[0], IMPLICATION)))
    {
        //get predicate and add the subject to precondition table as an implication
        Term subject = Term_ExtractSubterm(&event->term, 1);
        Term predicate = Term_ExtractSubterm(&event->term, 2);
        Concept *target_concept = Memory_Conceptualize(nar, &predicate, currentTime);
        if(target_concept != NULL)
        {
            target_concept->usage = Usage_use(target_concept->usage, currentTime, eternalInput);
            Implication imp = { .truth = eternal_event.truth,
                                .stamp = eternal_event.stamp,
                                .occurrenceTimeOffset = event->occurrenceTimeOffset,
                                .creationTime = currentTime };
            Term sourceConceptTerm = subject;
            //now extract operation id
            int opi = 0;
            if(Narsese_copulaEquals(nar, event->term.atoms[0], TEMPORAL_IMPLICATION) && Narsese_copulaEquals(nar, subject.atoms[0], SEQUENCE)) //sequence
            {
                Term potential_op = Term_ExtractSubterm(&subject, 2);
                if(Narsese_isOperation(nar, &potential_op)) //necessary to be an executable operator
                {
                    if(!Narsese_isExecutableOperation(nar, &potential_op))
                    {
                        return; //we can't store proc. knowledge of other agents
                    }
                    opi = Memory_getOperationID(nar, &potential_op); //"<(a * b) --> ^op>" to ^op index
                    sourceConceptTerm = Narsese_GetPreconditionWithoutOp(nar, &subject); //gets rid of op as MSC links cannot use it
                }
                else
                {
                    sourceConceptTerm = subject;
                }
            }
            else
            {
                sourceConceptTerm = subject;
            }
            Concept *source_concept = Memory_Conceptualize(nar, &sourceConceptTerm, currentTime);
            if(source_concept != NULL)
            {
                source_concept->usage = Usage_use(source_concept->usage, currentTime, eternalInput);
                imp.sourceConceptId = source_concept->id;
                imp.sourceConcept = source_concept;
                imp.term = event->term;
                Implication *revised = Table_AddAndRevise(nar, Narsese_copulaEquals(nar, event->term.atoms[0], IMPLICATION) ? &target_concept->implication_links : &target_concept->precondition_beliefs[opi], &imp);
                if(revised != NULL)
                {
                    bool wasRevised = revised->truth.confidence > event->truth.confidence || revised->truth.confidence == MAX_CONFIDENCE;
                    Memory_printAddedImplication(nar, &event->stamp, &event->term, &imp.truth, event->occurrenceTimeOffset, priority, input, false, true);
                    if(wasRevised)
                        Memory_printAddedImplication(nar, &revised->stamp, &revised->term, &revised->truth, revised->occurrenceTimeOffset, priority, input, true, true);
                }
            }
        }
    }
    bool Exclude = ((Narsese_copulaEquals(nar, event->term.atoms[0], IMPLICATION) || Narsese_copulaEquals(nar, event->term.atoms[0], EQUIVALENCE)) && ALLOW_IMPLICATION_EVENTS == 1 && !input) || //only input implications should be events
                   ((Narsese_copulaEquals(nar, event->term.atoms[0], IMPLICATION) || Narsese_copulaEquals(nar, event->term.atoms[0], EQUIVALENCE)) && ALLOW_IMPLICATION_EVENTS == 0); //no implications should be events
    if(!Narsese_copulaEquals(nar, event->term.atoms[0], TEMPORAL_IMPLICATION) && !Exclude)
    {
        Concept *c = Memory_Conceptualize(nar, &event->term, currentTime);
        if(c != NULL)
        {
            bool isContinuousPropertyStatement = Narsese_copulaEquals(nar, event->term.atoms[0], HAS_CONTINUOUS_PROPERTY) && !Narsese_copulaEquals(nar, event->term.atoms[1], PRODUCT);
            if(event->occurrenceTime != OCCURRENCE_ETERNAL && !isContinuousPropertyStatement)
            {
                OccurrenceTimeIndex_Add(c, &nar->occurrenceTimeIndex);
            }
            c->usage = Usage_use(c->usage, currentTime, eternalInput);
            c->priority = MAX(c->priority, priority);
            if(event->occurrenceTime != OCCURRENCE_ETERNAL && event->occurrenceTime <= currentTime)
            {
                if(ALLOW_NOT_SELECTED_PRECONDITIONS_CONDITIONING)
                {
                    c->lastSelectionTime = currentTime;
                }
                c->belief_spike = Inference_RevisionAndChoice(nar, &c->belief_spike, event, currentTime, NULL);
                c->belief_spike.creationTime = currentTime; //for metrics
                if(PRINT_SURPRISE && input)
                {
                    double surprise = 1.0;
                    if(c->predicted_belief.type != EVENT_TYPE_DELETED)
                    {
                        float expectation = Truth_Expectation(Truth_Projection(c->predicted_belief.truth, c->predicted_belief.occurrenceTime, c->belief_spike.occurrenceTime));
                        surprise = fabs(expectation - Truth_Expectation(c->belief_spike.truth));
                    }
                    printf("//SURPRISE %f\n", surprise);
                }
            }
            if(event->occurrenceTime != OCCURRENCE_ETERNAL && event->occurrenceTime > currentTime)
            {
                c->predicted_belief = Inference_RevisionAndChoice(nar, &c->predicted_belief, event, currentTime, NULL);
                c->predicted_belief.creationTime = currentTime;
            }
            bool revision_happened = false;
            if(eternalize)
            {
                //fputs("!!!", stdout); Narsese_PrintTerm(nar, &c->belief.term); puts("");
                c->belief = Inference_RevisionAndChoice(nar, &c->belief, &eternal_event, currentTime, &revision_happened);
                c->belief.creationTime = currentTime; //for metrics
            }
            if(input)
            {
                Memory_printAddedEvent(nar, &event->stamp, event, priority, input, false, false, true, false);
            }
            if(revision_happened)
            {
                Memory_AddEvent(nar, &c->belief, currentTime, priority, false, true, true, 0, false);
                if(event->occurrenceTime == OCCURRENCE_ETERNAL)
                {
                    Memory_printAddedEvent(nar, &c->belief.stamp, &c->belief, priority, false, false, true, true, false);
                }
            }
        }
    }
}

void Memory_RestrictDerivationsTo(NAR_t *nar, Term *term) //try to derive a certain target term
{
    IN_DEBUG( fputs("DERIVATION RESTRICTION SET TO ", stdout); Narsese_PrintTerm(nar, term); puts(""); )
    if(term == NULL)
    {
        nar->term_restriction = (Term) {0};
    }
    else
    {
        nar->term_restriction = *term;
    }
}

void Memory_AddEvent(NAR_t *nar, Event *event, long currentTime, double priority, bool input, bool derived, bool revised, int layer, bool eternalize)
{
    if(nar->term_restriction.atoms[0])
    {
        if(!Variable_Unify(nar, &nar->term_restriction, &event->term).success)
        {
            return;
        }
        else
        {
            IN_DEBUG( fputs("DERIVED CONCRETELY ", stdout); Narsese_PrintTerm(nar, &event->term); puts(""); )
        }
    }
    bool conceptHasToExist = false;
    if(Narsese_copulaEquals(nar, event->term.atoms[0], CONJUNCTION) || Narsese_copulaEquals(nar, event->term.atoms[0], IMPLICATION))
    {
        for(int i=0; !input && i<COMPOUND_TERM_SIZE_MAX; i++)
        {
            if(Narsese_copulaEquals(nar, event->term.atoms[i], INT_IMAGE1) ||
               Narsese_copulaEquals(nar, event->term.atoms[i], INT_IMAGE2) ||
               Narsese_copulaEquals(nar, event->term.atoms[i], EXT_IMAGE1) ||
               Narsese_copulaEquals(nar, event->term.atoms[i], EXT_IMAGE2))
            {
                conceptHasToExist = true;
                break;
            }
        }
    }
    for(int i=0; !nar->term_restriction.atoms[0] && !input && (event->stamp.evidentialBase[1] != STAMP_FREE || event->occurrenceTime != OCCURRENCE_ETERNAL) && i<COMPOUND_TERM_SIZE_MAX; i++)
    {
        if(Narsese_copulaEquals(nar, event->term.atoms[1], INT_IMAGE1) || Narsese_copulaEquals(nar, event->term.atoms[2], INT_IMAGE1) ||
           Narsese_copulaEquals(nar, event->term.atoms[1], INT_IMAGE2) || Narsese_copulaEquals(nar, event->term.atoms[2], INT_IMAGE2) ||
           Narsese_copulaEquals(nar, event->term.atoms[1], EXT_IMAGE1) || Narsese_copulaEquals(nar, event->term.atoms[2], EXT_IMAGE1) ||
           Narsese_copulaEquals(nar, event->term.atoms[1], EXT_IMAGE2) || Narsese_copulaEquals(nar, event->term.atoms[2], EXT_IMAGE2) ||
           Narsese_copulaEquals(nar, event->term.atoms[i], EXT_DIFFERENCE) ||
           Narsese_copulaEquals(nar, event->term.atoms[i], INT_DIFFERENCE) ||
           //(i*2+1 < COMPOUND_TERM_SIZE_MAX && Narsese_copulaEquals(nar, event->term.atoms[i], EXT_SET) && event->term.atoms[i*2+1] && event->term.atoms[i*2+1] != Narsese_CopulaIndex(SET_TERMINATOR)) ||
           //(i*2+1 < COMPOUND_TERM_SIZE_MAX && Narsese_copulaEquals(nar, event->term.atoms[i], INT_SET) && event->term.atoms[i*2+1] && event->term.atoms[i*2+1] != Narsese_CopulaIndex(SET_TERMINATOR)) ||
           Narsese_copulaEquals(nar, event->term.atoms[i], INT_INTERSECTION) ||
           Narsese_copulaEquals(nar, event->term.atoms[i], EXT_INTERSECTION) ||
           ((Variable_isDependentVariable(nar, event->term.atoms[i]) || event->occurrenceTime != OCCURRENCE_ETERNAL) && Narsese_copulaEquals(nar, event->term.atoms[0], CONJUNCTION))
           )
        {
            conceptHasToExist = true;
            break;
        }
    }
    if(conceptHasToExist && Memory_FindConceptByTerm(nar, &event->term) == NULL)
    {
        return;
    }
    if(Narsese_copulaEquals(nar, event->term.atoms[0], INHERITANCE) || Narsese_copulaEquals(nar, event->term.atoms[0], SIMILARITY))
    { //TODO maybe NAL term filter should go here?
        Term subject = Term_ExtractSubterm(&event->term, 1);
        Term predicate = Term_ExtractSubterm(&event->term, 2);
        if(Term_Equal(&subject, &predicate))
        {
            return;
        }
    }
    if(nar->RESTRICTED_CONCEPT_CREATION && !input && !Narsese_copulaEquals(nar, event->term.atoms[0], TEMPORAL_IMPLICATION) && Memory_FindConceptByTerm(nar, &event->term) == NULL)
    {
        return;
    }
    if(!revised && !input) //derivations get penalized by complexity as well, but revised ones not as they already come from an input or derivation
    {
        double complexity = Term_Complexity(&event->term);
        priority *= 1.0 / log2(1.0 + complexity);
    }
    if(event->truth.confidence < MIN_CONFIDENCE || priority <= MIN_PRIORITY || priority == 0.0)
    {
        return;
    }
    if(input && event->type == EVENT_TYPE_GOAL)
    {
        Memory_printAddedEvent(nar, &event->stamp, event, priority, input, false, false, true, false);
    }
    bool addedToCyclingEventsQueue = false;
    if(event->type == EVENT_TYPE_BELIEF)
    {

        if(!Narsese_copulaEquals(nar, event->term.atoms[0], TEMPORAL_IMPLICATION))
        {
            bool Exclude = ((Narsese_copulaEquals(nar, event->term.atoms[0], IMPLICATION) || Narsese_copulaEquals(nar, event->term.atoms[0], EQUIVALENCE)) && ALLOW_IMPLICATION_EVENTS == 1 && !input) || //only input implications should be events
                           ((Narsese_copulaEquals(nar, event->term.atoms[0], IMPLICATION) || Narsese_copulaEquals(nar, event->term.atoms[0], EQUIVALENCE)) && ALLOW_IMPLICATION_EVENTS == 0); //no implications should be events
            if(!Exclude && !(Narsese_copulaEquals(nar, event->term.atoms[0], HAS_CONTINUOUS_PROPERTY) && (Narsese_copulaEquals(nar, event->term.atoms[2], SIMILARITY) /*TODO, as it is =*/ || Narsese_copulaEquals(nar, event->term.atoms[2], SEQUENCE) /*TODO, as it is +*/)))
            {
                if(!Stamp_hasDuplicate(&event->stamp) && (input || !Narsese_copulaEquals(nar, event->term.atoms[0], CONJUNCTION)))
                {
                    addedToCyclingEventsQueue = Memory_addCyclingEvent(nar, event, priority, currentTime, layer);
                }
            }
        }
        Memory_ProcessNewBeliefEvent(nar, event, currentTime, priority, input, eternalize);
    }
    if(event->type == EVENT_TYPE_GOAL)
    {
        addedToCyclingEventsQueue = Memory_addCyclingEvent(nar, event, priority, currentTime, layer);
        assert(event->occurrenceTime != OCCURRENCE_ETERNAL, "Eternal goals are not supported");
    }
    if(addedToCyclingEventsQueue && !input) //print new tasks
    {
        Memory_printAddedEvent(nar, &event->stamp, event, priority, input, derived, revised, true, false);
    }
    assert(event->type == EVENT_TYPE_BELIEF || event->type == EVENT_TYPE_GOAL, "Erroneous event type");
}

void Memory_AddInputEvent(NAR_t *nar, Event *event, long currentTime)
{
    Memory_AddEvent(nar, event, currentTime, 1, true, false, false, 0, event->occurrenceTime == OCCURRENCE_ETERNAL || ALLOW_ETERNALIZATION == 2);
}

bool Memory_ImplicationValid(Implication *imp)
{
    return imp->sourceConceptId == ((Concept*) imp->sourceConcept)->id;
}

int Memory_getOperationID(NAR_t *nar, Term *term)
{
    Atom op_atom = Narsese_getOperationAtom(nar, term);
    if(op_atom)
    {
        for(int k=1; k<=OPERATIONS_MAX; k++)
        {
            if(nar->operations[k-1].term.atoms[0] == op_atom)
            {
                return k;
            }
        }
    }
    return 0;
}

Truth Memory_getTemporalLinkTruth(NAR_t *nar, Term *precondition, Term *postcondition)
{
    Concept *c = Memory_FindConceptByTerm(nar, postcondition);
    if(c != NULL)
    {
        for(int i=0; i<c->precondition_beliefs[0].itemsAmount; i++)
        {
            Implication *imp = &c->precondition_beliefs[0].array[i];
            Term imp_prec  = Term_ExtractSubterm(&imp->term, 1);
            Term imp_post  = Term_ExtractSubterm(&imp->term, 2);
            assert(Term_Equal(&imp_post, postcondition), "Link should not be in this table");
            if(Term_Equal(&imp_prec , precondition))
            {
                return imp->truth;
            }
        }
    }
    return (Truth) {0};
}
