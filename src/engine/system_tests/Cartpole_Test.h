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

static double velocity = 0.0;
static double angle = -3.1415/2.0;
static double angle_velocity = 0.0;
static double position = 0.0;
static double max_angle_velocity = 0.3;
static Feedback NAR_CP_Left(Term args)
{
    (void) args;
    double reverse = angle > 0 ? 1 : -1;
    //if(position > 0.0 && position < 1.0)
    {
        angle_velocity -= reverse * 0.2;
    }
    velocity -= 0.1;
    return (Feedback) {0};
}
static Feedback NAR_CP_Right(Term args)
{
    (void) args;
    double reverse = angle > 0 ? 1 : -1;
    //if(position > 0.0 && position < 1.0)
    {
        angle_velocity += reverse * 0.2;
    }
    velocity += 0.1;
    return (Feedback) {0};
}
static double successes = 0;
static double failures = 0;
void NAR_Cartpole(NAR_t *nar, long iterations)
{
    char initial[] = "                     |\n"
                     "                     |\n"
                     "     -----------     |\n"
                     "                     |\n"
                     "                     |\n";
    int t=0;
    puts(">>NAR CP start");
    NAR_AddOperation(nar, "^left", NAR_CP_Left);
    NAR_AddOperation(nar, "^right", NAR_CP_Right);
    while(1)
    {
        position += velocity;
        position = MAX(0.0, MIN(1.0, position));
        angle += angle_velocity;
        CLEAR_SCREEN;
        char world[sizeof(initial)];
        memcpy(world, initial, sizeof(initial));
        DRAW_LINE(10+position*5,2,angle,5,(char*) &world,'o');
        puts(world);
        //gravity
        angle_velocity += 0.2*cos(angle);
        //max. velocities given by air density
        if(angle_velocity > max_angle_velocity)
        {
            angle_velocity = max_angle_velocity;
        }
        if(angle_velocity < -max_angle_velocity)
        {
            angle_velocity = -max_angle_velocity;
        }
        //wrap around angle (where angle 1 corresponds to half a circle)
        double PI = 3.1415; //hm why M_PI doesn't work?
        if(angle > PI)
        {
            angle = -PI;
        }
        if(angle < -PI)
        {
            angle = PI;
        }
        if(t++ > iterations && iterations != -1)
        {
            break;
        }
        printf("position=%f, velocity=%f, angle=%f, angleV=%f\nsuccesses=%f, failures=%f, ratio=%f, time=%d\n", position, velocity, angle, angle_velocity, successes, failures, successes/(successes+failures), t);
        velocity = 0.0; //strong friction
        if(fabs(angle-(-PI/2.0)) <= 0.5) //in balance
        {
            NAR_AddInputNarsese(nar, "good. :|:");
            successes += 1.0;
        }
        else
        if(angle >= 0 && angle <= PI)
        {
            NAR_AddInputNarsese(nar, "good. :|: %0%");
            failures += 1.0;
        }
        char str[20] = {0};
        int encodingInt = (int)((angle+PI)/(2*PI)*8);
        sprintf(str, "%d. :|:", encodingInt);
        NAR_AddInputNarsese(nar, str);
        NAR_AddInputNarsese(nar, "good! :|:");
        //NAR_Cycles(nar, 3);
        fflush(stdout);
        if(iterations == -1)
        {
            SLEEP;SLEEP;SLEEP;
        }
    }
}
