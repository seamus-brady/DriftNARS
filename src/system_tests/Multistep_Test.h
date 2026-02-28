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

bool NAR_Lightswitch_GotoSwitch_executed = false;
Feedback NAR_Lightswitch_GotoSwitch(Term args)
{
    (void) args;
    NAR_Lightswitch_GotoSwitch_executed = true;
    puts("NAR invoked goto switch");
    return (Feedback) {0};
}
bool NAR_Lightswitch_ActivateSwitch_executed = false;
Feedback NAR_Lightswitch_ActivateSwitch(Term args)
{
    (void) args;
    NAR_Lightswitch_ActivateSwitch_executed = true;
    puts("NAR invoked activate switch");
    return (Feedback) {0};
}
void NAR_Multistep_Test(NAR_t *nar)
{
    puts(">>NAR Multistep test start");
    NAR_INIT(nar);
    nar->MOTOR_BABBLING_CHANCE = 0;
    NAR_AddOperation(nar, "^goto_switch", NAR_Lightswitch_GotoSwitch);
    NAR_AddOperation(nar, "^activate_switch", NAR_Lightswitch_ActivateSwitch);
    for(int i=0; i<5; i++)
    {
        NAR_AddInputBelief(nar, Narsese_AtomicTerm(nar, "start_at"));
        NAR_AddInputBelief(nar, Narsese_AtomicTerm(nar, "^goto_switch"));
        NAR_Cycles(nar, 1);
        NAR_AddInputBelief(nar, Narsese_AtomicTerm(nar, "switch_at"));
        NAR_AddInputBelief(nar, Narsese_AtomicTerm(nar, "^activate_switch"));
        NAR_AddInputBelief(nar, Narsese_AtomicTerm(nar, "switch_active"));
        NAR_Cycles(nar, 1);
        NAR_AddInputBelief(nar, Narsese_AtomicTerm(nar, "light_active"));
        NAR_Cycles(nar, 10);
    }
    NAR_Cycles(nar, 10);
    NAR_AddInputBelief(nar, Narsese_AtomicTerm(nar, "start_at"));
    NAR_AddInputGoal(nar, Narsese_AtomicTerm(nar, "light_active"));
    NAR_Cycles(nar, 10);
    assert(NAR_Lightswitch_GotoSwitch_executed && !NAR_Lightswitch_ActivateSwitch_executed, "NAR needs to go to the switch first");
    NAR_Lightswitch_GotoSwitch_executed = false;
    puts("NAR arrived at the switch");
    NAR_AddInputBelief(nar, Narsese_AtomicTerm(nar, "switch_at"));
    NAR_AddInputGoal(nar, Narsese_AtomicTerm(nar, "light_active"));
    assert(!NAR_Lightswitch_GotoSwitch_executed && NAR_Lightswitch_ActivateSwitch_executed, "NAR needs to activate the switch");
    NAR_Lightswitch_ActivateSwitch_executed = false;
    puts("<<NAR Multistep test successful");
}
