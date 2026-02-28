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

#include "Stats.h"
#include "NAR.h"

void Stats_Print(NAR_t *nar, long currentTime)
{
    double Stats_averageBeliefEventPriority = 0.0;
    for(int i=0; i<nar->cycling_belief_events.itemsAmount; i++)
    {
        Stats_averageBeliefEventPriority += nar->cycling_belief_events.items[i].priority;
    }
    Stats_averageBeliefEventPriority /= (double) CYCLING_BELIEF_EVENTS_MAX;
    double Stats_averageGoalEventPriority = 0.0;
    for(int layer=0; layer<CYCLING_GOAL_EVENTS_LAYERS; layer++)
    {
        for(int i=0; i<nar->cycling_goal_events[layer].itemsAmount; i++)
        {
            Stats_averageGoalEventPriority += nar->cycling_goal_events[layer].items[i].priority;
        }
    }
    Stats_averageGoalEventPriority /= (double) CYCLING_GOAL_EVENTS_MAX;
    double Stats_averageConceptPriority = 0.0;
    for(int i=0; i<nar->concepts.itemsAmount; i++)
    {
        Concept *c = nar->concepts.items[i].address;
        Stats_averageConceptPriority += c->priority;
    }
    Stats_averageConceptPriority /= (double) CONCEPTS_MAX;
    double Stats_averageConceptUsefulness = 0.0;
    int max_temporal_implication_table_items = 0;
    int max_declarative_implication_table_items = 0;
    for(int i=0; i<nar->concepts.itemsAmount; i++)
    {
        Stats_averageConceptUsefulness += nar->concepts.items[i].priority;
        Concept *c = nar->concepts.items[i].address;
        max_declarative_implication_table_items = MAX(max_declarative_implication_table_items, c->implication_links.itemsAmount);
        for(int opi=0; opi<=OPERATIONS_MAX; opi++)
        {
            max_temporal_implication_table_items = MAX(max_temporal_implication_table_items, c->precondition_beliefs[opi].itemsAmount);
        }
    }
    Stats_averageConceptUsefulness /= (double) CONCEPTS_MAX;
    puts("Statistics\n----------");
    printf("countConceptsMatchedTotal:\t%ld\n", nar->stats_concepts_matched_total);
    printf("countConceptsMatchedMax:\t%ld\n", nar->stats_concepts_matched_max);
    long countConceptsMatchedAverage = nar->stats_concepts_matched_total / currentTime;
    printf("countConceptsMatchedAverage:\t%ld\n", countConceptsMatchedAverage);
    printf("currentTime:\t\t\t%ld\n", currentTime);
    printf("total concepts:\t\t\t%d\n", nar->concepts.itemsAmount);
    printf("DeclarativeImplicationTableMaxItems:\t%d\n", max_declarative_implication_table_items);
    printf("TemporalImplicationTableMaxItems:\t%d\n", max_temporal_implication_table_items);
    printf("current average concept priority:\t%f\n", Stats_averageConceptPriority);
    printf("current average concept usefulness:\t%f\n", Stats_averageConceptUsefulness);
    printf("current belief events cnt:\t\t%d\n", nar->cycling_belief_events.itemsAmount);
    int goal_events_cnt = 0;
    for(int layer=0; layer<CYCLING_GOAL_EVENTS_LAYERS; layer++)
    {
        goal_events_cnt += nar->cycling_goal_events[layer].itemsAmount;
    }
    printf("current goal events cnt:\t\t%d\n", goal_events_cnt);
    printf("Count atomic terms used:\t\t%d\n", Narsese_CountAtomsUsed(nar));
    printf("current average belief event priority:\t%f\n", Stats_averageBeliefEventPriority);
    printf("current average goal event priority:\t%f\n", Stats_averageGoalEventPriority);
    printf("Maximum chain length in concept hashtable: %d\n", HashTable_MaximumChainLength(&nar->HTconcepts));
    printf("Maximum chain length in atoms hashtable: %d\n", HashTable_MaximumChainLength(&nar->HTatoms));
    fflush(stdout);
}
