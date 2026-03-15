/*
 * The MIT License
 *
 * Copyright 2024 DriftNARS authors.
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

#include <stdio.h>
#include <unistd.h>

void Serialization_Test(NAR_t *nar)
{
    puts(">>Serialization Test start");

    /* Set up a fresh instance and teach it some knowledge */
    NAR_INIT(nar);
    nar->PRINT_INPUT = false;
    nar->PRINT_DERIVATIONS = false;

    NAR_AddInputNarsese(nar, "<bird --> animal>.");
    NAR_AddInputNarsese(nar, "<robin --> bird>.");
    NAR_Cycles(nar, 10);

    /* Remember key state before save */
    long saved_time = nar->currentTime;
    int saved_concept_count = nar->concepts.itemsAmount;
    int saved_term_index = nar->term_index;

    /* Verify robin-->animal was derived */
    Term t_robin_animal = Narsese_Term(nar, "<robin --> animal>");
    Concept *c_before = Memory_FindConceptByTerm(nar, &t_robin_animal);
    assert(c_before != NULL, "Concept <robin --> animal> should exist before save");
    Truth truth_before = c_before->belief.truth;
    assert(truth_before.confidence > 0.0, "Should have non-zero confidence before save");

    /* Save state */
    const char *path = "/tmp/driftnars_test.dnar";
    int rc = NAR_Save(nar, path);
    assert(rc == NAR_OK, "NAR_Save should succeed");

    /* Reset and verify it's clean */
    NAR_INIT(nar);
    nar->PRINT_INPUT = false;
    nar->PRINT_DERIVATIONS = false;

    /* Load state */
    rc = NAR_Load(nar, path);
    assert(rc == NAR_OK, "NAR_Load should succeed");

    /* Verify state was restored */
    assert(nar->currentTime == saved_time, "currentTime should be restored");
    assert(nar->concepts.itemsAmount == saved_concept_count, "concept count should be restored");
    assert(nar->term_index == saved_term_index, "term_index should be restored");

    /* Verify concepts and beliefs are intact */
    Term t_robin_animal2 = Narsese_Term(nar, "<robin --> animal>");
    Concept *c_after = Memory_FindConceptByTerm(nar, &t_robin_animal2);
    assert(c_after != NULL, "Concept <robin --> animal> should exist after load");
    Truth truth_after = c_after->belief.truth;
    assert(truth_after.frequency == truth_before.frequency, "Frequency should match after load");
    assert(truth_after.confidence == truth_before.confidence, "Confidence should match after load");

    /* Verify the system can still reason after load */
    NAR_AddInputNarsese(nar, "<cat --> animal>.");
    NAR_Cycles(nar, 5);
    Term t_cat = Narsese_Term(nar, "<cat --> animal>");
    Concept *c_cat = Memory_FindConceptByTerm(nar, &t_cat);
    assert(c_cat != NULL, "Should be able to add new concepts after load");

    /* Verify error handling */
    rc = NAR_Load(nar, "/tmp/nonexistent_file.dnar");
    assert(rc == NAR_ERR_IO, "Loading nonexistent file should return NAR_ERR_IO");

    /* Clean up */
    unlink(path);

    /* ── Compact test ────────────────────────────────────── */
    NAR_INIT(nar);
    nar->PRINT_INPUT = false;
    nar->PRINT_DERIVATIONS = false;

    /* Add enough inputs to create many concepts */
    char compact_buf[64];
    for(int i = 0; i < 30; i++)
    {
        sprintf(compact_buf, "<item%d --> thing>.", i);
        NAR_AddInputNarsese(nar, compact_buf);
    }
    int before_count = nar->concepts.itemsAmount;
    int before_alloc = nar->concepts_allocated;
    assert(before_count > 20, "Should have many concepts after 30 inputs");

    /* Compact down to 5 */
    int result = NAR_Compact(nar, 5);
    assert(result == 5, "Compact should return target count");
    assert(nar->concepts.itemsAmount == 5, "PQ should have 5 items");
    assert(nar->concepts_allocated == 5, "Should have 5 allocated concepts");
    assert(nar->concepts_allocated < before_alloc, "Should have fewer allocated concepts");

    /* System should still work after compact */
    NAR_AddInputNarsese(nar, "<dog --> animal>.");
    assert(nar->concepts.itemsAmount >= 5, "Should be able to add concepts after compact");
    assert(nar->concepts_allocated >= 5, "Allocated count should grow after compact");

    puts(">>Serialization Test successful");
}
