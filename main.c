//
//  main.c
//  randTasks.c
//
//  Created by Justin Nguyen on 8/10/18.
//  Copyright © 2018 Justin Nguyen. All rights reserved.
//

#include <litmus.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <float.h>
#include <string.h>
#include <math.h>

// error checking macro
#define CALL(exp) do{ \
int ret; \
ret = exp; \
if(ret!=0) \
fprintf(stderr, "%s failed: $m\n", #exp); \
else \
fprintf(stderr, "%s ok.\n", #exp); \
} while(0)

// error checker used for set_rt_task_param() (where tasks often fail)
// return value utilized to calculate schedulability and exec ratio of task sets
int AltCALL(int exp)
{
    do{
        int ret;
        ret = exp;
        if(ret!=0)
        {
            printf("%s\n", "set_rt_task_param() failed");
            return 0;
        }
        else
        {
            printf("%s\n", "set_rt_task_param() ok.");
            return 1;
        }
    } while(0);
}

// run jobs for a real-time task
int i= 0;
int job(void)
{
    i++;
    if(i >= 5)
        return 1;
    return 0;
}

// function which runs one real-time task
// WCET = worst-case execution time, period = task period, deadline = task deadline, taskSetExec = variable used in genRandTaskSets() function
void runTask(long long int WCET, long long int period, long long int deadline, float *taskSetExec)
{
    //printf("%d\n", gettid());
    int do_exit;
    struct rt_task params;
    CALL(init_litmus());
    init_rt_task_param(&params);
    
    long long int EXEC_COST = WCET;
    long long int PERIOD = period;
    long long int DEADLINE = deadline;
    
    params.exec_cost = EXEC_COST;
    params.period = PERIOD;
    params.relative_deadline = DEADLINE;
    
    printf("Exec cost: %llu\n", params.exec_cost);
    printf("Period: %llu\n", params.period);
    printf("Relative deadline: %llu\n", params.relative_deadline);
    
    *taskSetExec += AltCALL(set_rt_task_param(gettid(), &params));
    CALL(task_mode(LITMUS_RT_TASK));
    
    //CALL(wait_for_ts_release());
    long long int clock = litmus_clock();
    long long int *clockPtr = &clock;
    //printf("%d\n", get_nr_ts_release_waiters());
    CALL(release_ts(clockPtr));
    do{
        sleep_next_period();
        do_exit = job();
        //printf("%d\n",do_exit);
    }while(!do_exit);
    CALL(task_mode(BACKGROUND_TASK));
    printf("\n");
}

// function used to generate random WCET values
// density = WCET/(min(deadline,period))
// WCET = density*(min(deadline,period))
// n = num of tasks in taskset, densityArr = array already containing density values, deadlineArr = array already containing deadline values, WCETArr = array to store WCET values
void gen_WCET(int n, float** densityArr, long long int deadlineArr[], long long int WCETArr[])
{
    float value = 0;
    for(int x = 0; x < n; x++)
    {
        value = (densityArr[x][0] * deadlineArr[x]) + 0.5;
        WCETArr[x] = (long long int)(value);
    }
}

// function used to generate random deadline values
// deadline <= period
// n = num of tasks in task set, periodArr = array already containing period values, deadlineArr = array to store deadline values
void gen_deadlines(int n, long long int periodArr[], long long int deadlineArr[])
{
    srand(time(NULL));
    long long int low = 100;
    float randValue = 0, round = 0, scale = 0;
    for(int x = 0; x < n; x++)
    {
        float high = periodArr[x];
        scale = rand() / (float)(RAND_MAX);
        randValue = low + scale*(high-low);
        round = randValue + 0.5;
        deadlineArr[x] = (long long int)(round);
    }
}

// function used to generate random period values
// n = num of tasks per task set, min = min period value, max = max period value, gran = granularity, periodArr = array to store period values, dist = type of distribution of values("logunif" or "uniform")
// min and max are multiples of granularity
void gen_periods(int n, float min, float max, float gran, long long int periodArr[], char *dist)
{
    if(dist == "logunif")
    {
        float low = log(min);
        float high = log(max + gran);
        float randValue = 0, round = 0, scale = 0;
        srand(time(NULL));
        for(int x = 0; x < n; x++)
        {
            scale = rand() / (float)(RAND_MAX);// [0,1]
            randValue = low + scale*(high-low);// [low,high]
            randValue = exp(randValue); // e^(randValue)
            //printf("%f ", randValue);
            randValue = floor(randValue / gran) * gran;
            round = randValue + 0.5;
            periodArr[x] = (long long int)(round);
            //printf("%f ", randValue);
        }
    }
    else if(dist == "uniform")
    {
        float low = min;
        float high = max + gran;
        float randValue = 0, round = 0, scale = 0;
        srand(time(NULL));
        for(int x = 0; x < n; x++)
        {
            scale = rand() / (float)(RAND_MAX);// [0,1]
            randValue = low + scale*(high-low);// [low,high]
            randValue = floor(randValue / gran) * gran;
            round = randValue + 0.5;
            periodArr[x] = (long long int)(round);
        }
    }
}

// function used to randomly permute array at end of StaffordRandFixedSum()
// arr = array to randomize, n = num of elements in array
void randomize(float arr[], int n)
{
    for(int x = n-1; x > 0; x--)
    {
        int y = rand() % (x+1);
        float temp = arr[x];
        arr[x] = arr[y];
        arr[y] = temp;
    }
}

// C implementation of Roger Stafford's RandFixedSum algorithm
// algorithm used to generate random density values for each task within different task sets, with each task set containing tasks whose density values add up to same fixed sum (total utilization ratio for one task set)
// Stafford's original function returns 2D array
// void function below passes in double pointer to change 2D array
// n = num of tasks per task set, u = total utilization ratio, nsets = num of task sets, arr = 2D array to store density values
// u must be smaller than n
void StaffordRandFixedSum(int n, float u, int nsets, float** arr)
{
    // check arguments
    if(n < 0 || nsets < 0)
    {
        printf("Invalid argument(s): n and nsets must be non-negative integers.\n");
        exit(0);
    }
    
    // deal with n=1 case
    if(n == 1)
    {
        for(int x = 0; x < 1; x++)
            for(int y = 0; y < nsets; y++)
                arr[x][y] = u;
    }
    
    else
    {
        // generate transition probability table
        
        float k = floor(u);
        float s = u;
        
        // construct arrays with evenly spaced values
        // similar to numpy.arange()
        int start = 0, stop = 0, step = 0, numOfValues = 0, value = 0;
        if(k < (k-n+1))
            step = 1;
        else
            step = -1;
        start = k;
        stop = k-n+1;
        numOfValues = (int)(((stop-start)/step) + 1);
        value = start;
        
        float s1[numOfValues];
        for(int x = 0; x < numOfValues; x++)
        {
            s1[x] = s - value;
            //printf("%f ", s1[x]);
            value = value + step;
        }
        
        //printf("\n");
        if((k+n) < (k-n+1))
            step = 1;
        else
            step = -1;
        start = k + n;
        stop = k + 1;
        numOfValues = (int)(((stop-start)/step) + 1);
        value = start;
        
        float s2[numOfValues];
        for(int x = 0; x < numOfValues; x++)
        {
            s2[x] = value - s;
            //printf("%f ", s2[x]);
            value = value + step;
        }
        //printf("\n");
        
        float tiny = FLT_MIN; // smallest possible float
        float huge = FLT_MAX; // largest possible float
        
        // generate 1st 2D array of zeros (like numpy.zeros())
        float w[n][n+1];
        for(int x = 0; x < n; x++)
        {
            for(int y = 0; y < n+1; y++)
            {
                w[x][y] = 0;
                //printf("%f ", w[x][y]);
            }
            //printf("\n");
        }
        w[0][1] = huge;
        //printf("%f", w[0][1]);
        
        // generate 2nd 2D array of zeros
        float t[n-1][n];
        for(int x = 0; x < n-1; x++)
        {
            for(int y = 0; y < n; y++)
            {
                t[x][y] = 0;
                //printf("%f ", t[x][y]);
            }
            //printf("\n");
        }
        
        float tmp1[n], tmp2[n], tmp3[n], tmp4[n];
        // for loop containing tmp1, tmp2, etc...
        for(int x = 2; x < n+1; x++)
        {
            // tmp1, tmp2
            int index = 1; // index for tmp1
            int index2 = n-x; // index for tmp2
            for(int y = 0; y < x; y++)
            {
                tmp1[y] = w[x-2][index] * s1[y] / (float)(x);
                tmp2[y] = w[x-2][y] * s2[index2] / (float)(x);
                index++;
                index2++;
                //printf("%f ", w[x-2][y]);
            }
            
            // python: w[i-1, numpy.arange(1,(i+1))] = tmp1 + tmp2
            for(int z = 1; z < x+1; z++)
            {
                //printf("\n");
                w[x-1][z] = tmp1[z-1] + tmp2[z-1];
                //printf("%f ", w[x-1][z]);
            }
            
            // tmp3
            for(int z = 1; z < x+1; z++)
            {
                tmp3[z-1] = w[x-1][z] + tiny;
            }
            
            // tmp4
            int tmp4 = 0; // represents boolean value
            int tmp4Not = 1; // represents numpy.logical_not(tmp4)
            float sumOfS1 = 0, sumOfS2 = 0;
            for(int y = n-x; y < n; y++)
                sumOfS2 += s2[y];
            for(int y = 0; y < x; y++)
                sumOfS1 += s1[y];
            if(sumOfS2 > sumOfS1)
            {
                tmp4 = 1;
                tmp4Not = 0;
            }
            
            // change t array
            for(int y = 0; y < x; y++)
            {
                t[x-2][y] = (tmp2[y]/tmp3[y])*tmp4 + (1-tmp1[y]/tmp3[y])*tmp4Not;
                //printf("%f ", t[x-2][y]);
            }
            //printf("\n");
            
        }
        
        int m = nsets;
        float finalArray[n][m]; // n x m array of zeros
        for(int y = 0; y < n; y++)
        {
            for(int z = 0; z < m; z++)
                finalArray[y][z] = 0;
        }
        
        float rt[n-1][m]; // rand simplex type
        float rs[n-1][m]; // rand position in simplex
        
        // initialize rt and rs with rand values btwn 0 and 1
        // like numpy.random.uniform()
        srand(time(0));
        for(int y = 0; y < n-1; y++)
        {
            for(int z = 0; z < m; z++)
            {
                rt[y][z] = (float)(rand())/RAND_MAX;
                //printf("%f ", rt[y][z]);
            }
            //printf("\n");
        }
        //printf("\n");
        for(int y = 0; y < n-1; y++)
        {
            for(int z = 0; z < m; z++)
            {
                rs[y][z] = (float)(rand())/RAND_MAX;
                //printf("%f ", rs[y][z]);
            }
            //printf("\n");
        }
        
        // create arrays using numpy.repeat
        float s3[m], sm[m], pr[m];
        int j[m];
        for(int x = 0; x < m; x++)
        {
            s3[x] = s;
            //printf("%f ", s3[x]);
        }
        //printf("\n");
        for(int x = 0; x < m; x++)
        {
            j[x] = (int)(k+1);
            //printf("%d ", j[x]);
        }
        //printf("\n");
        for(int x = 0; x < m; x++)
        {
            sm[x] = 0;
            //printf("%f ", sm[x]);
        }
        //printf("\n");
        for(int x = 0; x < m; x++)
        {
            pr[x] = 1;
            //printf("%f ", pr[x]);
        }
        
        /*for(int x = 0; x < n-1; x++)
         {
         for(int y = 0; y < n; y++)
         {
         printf("%f ", t[x][y]);
         }
         printf("\n");
         }*/
        
        // iterate through dimensions
        int e = 0; // either 1 or 0
        for(int x = n-1; x > 0; x--)
        {
            float sumOf_rt = 0; // used to compare rt and t
            float sumOf_t = 0;
            for(int y = 0; y < m; y++)
            {
                sumOf_rt += rt[n-x-1][y];
                sumOf_t += t[x-1][j[y]-1];
            }
            // decide which direction to move in this dimension
            if(sumOf_rt <= sumOf_t)
                e = 1;
            else
                e = 0;
            //printf("%d\n", e);
            
            float sx[m]; // next simplex coordinate
            for(int y = 0; y < m; y++)
            {
                sx[y] = pow(rs[n-x-1][y],(1/(float)(x)));
                //printf("%f ", sx[y]);
                sm[y] = sm[y]+(1-sx[y])*pr[y]*s3[y]/(float)(x+1);
                //printf("%f ", sm[y]);
                pr[y] = sx[y] * pr[y];
                //printf("%f ", pr[y]);
                finalArray[n-x-1][y] = sm[y] + pr[y] * e;
                //printf("%f ", finalArray[n-x-1][y]);
                s3[y] = s3[y] - e;
                //printf("%f ", s3[y]);
                j[y] = j[y] - e;
                //printf("%d ", j[y]);
            }
            //printf("\n");
        }
        for(int x = 0; x < m; x++)
        {
            finalArray[n-1][x] = sm[x] + pr[x] * s3[x];
        }
        
        // randomly permute order of column values in finalArray
        // must set seed value outside loop to avoid getting same random values on each loop iteration
        srand(time(NULL));
        float tempArr[n];
        for(int x = 0; x < m; x++)
        {
            for(int y = 0; y < n; y++)
            {
                tempArr[y] = finalArray[y][x];
                //printf("%f ", tempArr[y]);
            }
            //printf("\n");
            randomize(tempArr, n);
            for(int y = 0; y < n; y++)
            {
                finalArray[y][x] = tempArr[y];
                //printf("%f ", tempArr[y]);
            }
        }
        
        // store values of finalArray in arr
        float finalArraySum = 0;
        float columnSum = 0;
        for(int x = 0; x < n; x++)
        {
            for(int y = 0; y < m; y++)
            {
                //printf("%f ", finalArray[x][y]);
                finalArraySum += finalArray[x][y];
                arr[x][y] = finalArray[x][y];
            }
            //printf("\n");
        }
        //printf("Sum: %f\n\n", finalArraySum);
    }
    
}

// function that generates task sets, each with random num of tasks/ each task has random parameter values
// nsets = num of task sets
// prints percentage of schedulable task sets and average execution ratio of task sets generated
void genRandTaskSets(int nsets)
{
    float sched = 0, exec = 0; // schedulability and execution ratio
    float taskSetExec = 0; // exec ratio for one task set
    
    srand(time(NULL));
    int lower = 1, upper = 10;
    // each iteration of loop is generation of one task set
    for(int x = 0; x < nsets; x++)
    {
        taskSetExec = 0;
        printf("\n%s\n", "New task set:");
        int numTasks = rand() % upper; // generate random num of tasks for each task set
        if(numTasks < lower)
            numTasks += lower;
        printf("%d\n", numTasks);
        
        float** densityArr;
        densityArr = malloc(numTasks * sizeof * densityArr);
        for(int x = 0; x < numTasks; x++)
        {
            densityArr[x] = malloc(1 * sizeof * densityArr[x]);
        }
        StaffordRandFixedSum(numTasks, 5, nsets, densityArr);
        
        long long int WCETArr[numTasks];
        long long int periodArr[numTasks];
        long long int deadlineArr[numTasks];
        gen_periods(numTasks, 100, 10000, 0.001, periodArr, "uniform");
        gen_deadlines(numTasks, periodArr, deadlineArr);
        gen_WCET(numTasks, densityArr, deadlineArr, WCETArr);
        
        for(int y = 0; y < numTasks; y++)
        {
            runTask(WCETArr[y], periodArr[y], deadlineArr[y], &taskSetExec);
            /*printf("%s ", "Density:");
             printf("%f\n", densityArr[y][0]);
             */
        }
        exec += (taskSetExec/numTasks);
        if(taskSetExec == numTasks)
            sched += 1;
    }
    sched = sched/nsets;
    exec = exec/nsets;
    printf("Schedulability: %f\n", sched);
    printf("Execution Ratio: %f\n", exec);
}
int main()
{
    genRandTaskSets(4);
    
    return 0;
}
