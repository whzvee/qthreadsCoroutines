#include <stdio.h>		       /* for printf() */
#include <assert.h>		       /* for assert() */
#include <qthread/qthread.h>
#include "qtimer/qtimer.h"

#define ITERATIONS 1000000
#define MAXPARALLELISM 64

aligned_t FEBbuffer[MAXPARALLELISM] = { 0 };
aligned_t FEBtable[MAXPARALLELISM][2] = { { 0 } };

qtimer_t sending[MAXPARALLELISM][2];
double total_sending_time[MAXPARALLELISM] = { 0.0 };
double total_roundtrip_time[MAXPARALLELISM] = { 0.0 };
double total_p1_sending_time[MAXPARALLELISM] = { 0.0 };
double total_p2_sending_time[MAXPARALLELISM] = { 0.0 };

aligned_t incrementme = 0;

aligned_t FEB_consumer(qthread_t * me, void *arg)
{
    aligned_t pong = 0;

    assert(qthread_readFE(me, &pong, arg) == 0);
    if (pong != 1) {
	printf("pong = %u\n", (unsigned)pong);
	assert(pong == 1);
    }
    return pong;
}

aligned_t FEB_producer(qthread_t * me, void *arg)
{
    aligned_t ping = 1;

    assert(qthread_writeEF(me, arg, &ping) == 0);
    return ping;
}

aligned_t FEB_producerloop(qthread_t * me, void *arg)
{
    unsigned int offset = (unsigned int) arg;
    aligned_t timer = 0;
    unsigned int i;

    for (i = 0; i < ITERATIONS; i++) {
	qtimer_start(sending[offset][timer]);
	assert(qthread_writeEF(me, FEBbuffer+offset, &timer) == 0);
	timer = timer ? 0 : 1;
    }
    return 0;
}

aligned_t FEB_consumerloop(qthread_t * me, void *arg)
{
    unsigned int offset = (unsigned int) arg;
    aligned_t timer = 0;
    unsigned int i;

    for (i = 0; i < ITERATIONS; i++) {
	assert(qthread_readFE(me, &timer, FEBbuffer+offset) == 0);
	qtimer_stop(sending[offset][timer]);
	total_sending_time[offset] += qtimer_secs(sending[offset][timer]);
    }
    return 0;
}

aligned_t FEB_player2(qthread_t * me, void *arg)
{
    unsigned int offset = (unsigned int) arg;
    aligned_t paddle = 0;
    unsigned int i;

    for (i = 0; i < ITERATIONS; i++) {
	assert(qthread_readFE(me, &paddle, FEBtable[offset]) == 0);
	qtimer_stop(sending[offset][0]);

	total_p1_sending_time[offset] += qtimer_secs(sending[offset][0]);

	qtimer_start(sending[offset][1]);
	assert(qthread_writeEF(me, FEBtable[offset] + 1, &paddle) == 0);
    }
    return 0;
}

aligned_t FEB_player1(qthread_t * me, void *arg)
{
    unsigned int offset = (unsigned int) arg;
    aligned_t paddle = 1;
    unsigned int i;
    qtimer_t roundtrip_timer = qtimer_new();

    /* serve */
    qtimer_start(sending[offset][0]);
    qtimer_start(roundtrip_timer);
    assert(qthread_writeEF(me, FEBtable[offset], &paddle) == 0);

    for (i = 0; i < ITERATIONS; i++) {
	assert(qthread_readFE(me, &paddle, FEBtable[offset] + 1) == 0);
	qtimer_stop(sending[offset][1]);
	qtimer_stop(roundtrip_timer);

	total_roundtrip_time[offset] += qtimer_secs(roundtrip_timer);
	total_p2_sending_time[offset] += qtimer_secs(sending[offset][1]);

	if (i + 1 < ITERATIONS) {
	    qtimer_start(sending[offset][0]);
	    qtimer_start(roundtrip_timer);
	    assert(qthread_writeEF(me, FEBtable[offset], &paddle) == 0);
	}
    }
    qtimer_free(roundtrip_timer);
    return 0;
}

aligned_t incrloop(qthread_t * me, void *arg)
{
    unsigned int i;

    for (i = 0; i < ITERATIONS; i++) {
	qthread_incr(&incrementme, 1);
    }
    return 0;
}

char *human_readable_rate(double rate)
{
    static char readable_string[100] = { 0 };
    const double GB = 1024 * 1024 * 1024;
    const double MB = 1024 * 1024;
    const double kB = 1024;

    if (rate > GB) {
	snprintf(readable_string, 100, "(%'.1f GB/s)", rate / GB);
    } else if (rate > MB) {
	snprintf(readable_string, 100, "(%'.1f MB/s)", rate / MB);
    } else if (rate > kB) {
	snprintf(readable_string, 100, "(%'.1f kB/s)", rate / kB);
    }
    return readable_string;
}

int main(int argc, char *argv[])
{
    qtimer_t timer = qtimer_new();
    double rate;
    unsigned int i;
    aligned_t rets[MAXPARALLELISM];

    /* setup */
    qthread_init(MAXPARALLELISM);
    for (i=0;i<MAXPARALLELISM;i++) {
	qthread_empty(NULL, FEBbuffer+i);
	sending[i][0] = qtimer_new();
	sending[i][1] = qtimer_new();
    }
    qthread_empty(NULL, FEBtable);
    qthread_empty(NULL, FEBtable + 1);
    printf("Testing producer/consumer:\n");

    /* SINGLE FEB SEND/RECEIVE TEST */
    qtimer_start(timer);
    qthread_fork(FEB_consumer, FEBbuffer, rets);
    qthread_fork(FEB_producer, FEBbuffer, NULL);
    qthread_readFF(NULL, NULL, rets);
    qtimer_stop(timer);

    printf("\tSingle FEB send/receive: %19g secs\n", qtimer_secs(timer));
    rate = sizeof(aligned_t) / qtimer_secs(timer);
    printf("\t = throughput: %29g bytes/sec %s\n", rate,
	   human_readable_rate(rate));

    /* PARALLEL SINGLE FEB SEND/RECEIVE TEST */
    qtimer_start(timer);
    for (i=0; i<MAXPARALLELISM; i++) {
	qthread_fork(FEB_consumer, FEBbuffer+i, rets+i);
	qthread_fork(FEB_producer, FEBbuffer+i, NULL);
    }
    for (i=0; i<MAXPARALLELISM; i++) {
	qthread_readFF(NULL, NULL, rets+i);
    }
    qtimer_stop(timer);

    printf("\tParallel single FEB send/receive: %10g secs (%u parallel)\n", qtimer_secs(timer), MAXPARALLELISM);
    rate = (MAXPARALLELISM*sizeof(aligned_t)) / qtimer_secs(timer);
    printf("\t = throughput: %29g bytes/sec %s\n", rate,
	   human_readable_rate(rate));

    /* FEB PRODUCER/CONSUMER LOOP */
    qtimer_start(timer);
    qthread_fork(FEB_consumerloop, NULL, rets);
    qthread_fork(FEB_producerloop, NULL, NULL);
    qthread_readFF(NULL, NULL, rets);
    qtimer_stop(timer);

    printf("\tFEB producer/consumer loop: %16g secs (%u iterations)\n", qtimer_secs(timer), (unsigned)ITERATIONS);
    printf("\t - total sending time: %21g secs\n", total_sending_time[0]);
    printf("\t + external average time: %18g secs\n",
	   qtimer_secs(timer) / ITERATIONS);
    printf("\t + internal average time: %18g secs\n",
	   total_sending_time[0] / ITERATIONS);
    printf("\t = message throughput: %21g msgs/sec\n",
	   ITERATIONS / total_sending_time[0]);
    rate = (ITERATIONS * sizeof(aligned_t)) / total_sending_time[0];
    printf("\t = data throughput: %24g bytes/sec %s\n", rate,
	   human_readable_rate(rate));

    /* PARALLEL FEB PRODUCER/CONSUMER LOOPS */
    qtimer_start(timer);
    for (i=0; i<MAXPARALLELISM; i++) {
	qthread_fork(FEB_consumerloop, (void*)i, rets+i);
    }
    for (i=0; i<MAXPARALLELISM; i++) {
	qthread_fork(FEB_producerloop, (void*)i, NULL);
    }
    for (i=0; i<MAXPARALLELISM; i++) {
	qthread_readFF(NULL, NULL, rets+i);
    }
    qtimer_stop(timer);

    for (i=1;i<MAXPARALLELISM;i++) {
	total_sending_time[0] += total_sending_time[i];
    }
    printf("\tParallel FEB producer/consumer loop: %6g secs (%u-way %u iters)\n", qtimer_secs(timer), MAXPARALLELISM, ITERATIONS);
    printf("\t - total sending time: %21g secs\n", total_sending_time[0]);
    printf("\t + external average time: %18g secs\n",
	   qtimer_secs(timer) / (ITERATIONS*MAXPARALLELISM));
    printf("\t + internal average time: %18g secs\n",
	   total_sending_time[0] / (ITERATIONS*MAXPARALLELISM));
    printf("\t = message throughput: %21g msgs/sec\n",
	   (ITERATIONS*MAXPARALLELISM) / qtimer_secs(timer));
    rate = (ITERATIONS*MAXPARALLELISM * sizeof(aligned_t)) / qtimer_secs(timer);
    printf("\t = data throughput: %24g bytes/sec %s\n", rate,
	   human_readable_rate(rate));

    /* FEB PING-PONG LOOP */
    qtimer_start(timer);
    qthread_fork(FEB_player2, NULL, rets);
    qthread_fork(FEB_player1, NULL, NULL);
    qthread_readFF(NULL, NULL, rets);
    qtimer_stop(timer);

    printf("\tFEB ping-pong loop: %24g secs (%u round trips)\n", qtimer_secs(timer), ITERATIONS);
    printf("\t - total rtts: %29g secs\n", total_roundtrip_time[0]);
    printf("\t - total sending time: %21g secs\n",
	   total_p1_sending_time[0] + total_p2_sending_time[0]);
    printf("\t + external avg rtt: %23g secs\n",
	   qtimer_secs(timer) / ITERATIONS);
    printf("\t + internal avg rtt: %23g secs\n",
	   total_roundtrip_time[0] / ITERATIONS);
    printf("\t + average p1 sending time: %16g secs\n",
	   total_p1_sending_time[0] / ITERATIONS);
    printf("\t + average p2 sending time: %16g secs\n",
	   total_p2_sending_time[0] / ITERATIONS);
    printf("\t + average sending time: %19g secs\n",
	   (total_p1_sending_time[0] +
	    total_p2_sending_time[0]) / (ITERATIONS * 2));
    /* each rt is 2 messages, thus: */
    printf("\t = messaging throughput: %19g msgs/sec\n",
	   (ITERATIONS * 2) / total_roundtrip_time[0]);
    /* each rt is 1 message of aligned_t bytes each, thus: */
    rate = (ITERATIONS * sizeof(aligned_t)) / total_roundtrip_time[0];
    printf("\t = data roundtrip tput: %20g bytes/sec %s\n", rate,
	   human_readable_rate(rate));
    /* each send is 1 messsage of aligned_t bytes, thus: */
    rate = (ITERATIONS * sizeof(aligned_t)) / total_p1_sending_time[0];
    printf("\t = p1 hop throughput: %22g bytes/sec %s\n", rate,
	   human_readable_rate(rate));
    rate = (ITERATIONS * sizeof(aligned_t)) / total_p2_sending_time[0];
    printf("\t = p2 hop throughput: %22g bytes/sec %s\n", rate,
	   human_readable_rate(rate));
    rate =
	(ITERATIONS * 2 * sizeof(aligned_t)) / (total_p1_sending_time[0] +
						total_p2_sending_time[0]);
    printf("\t = data hop throughput: %20g bytes/sec %s\n", rate,
	   human_readable_rate(rate));

    /* PARALLEL FEB PING-PONG LOOP */
    qtimer_start(timer);
    for (i=0; i<MAXPARALLELISM; i++) {
	qthread_fork(FEB_player2, (void*)i, rets+i);
    }
    for (i=0; i<MAXPARALLELISM; i++) {
	qthread_fork(FEB_player1, (void*)i, NULL);
    }
    for (i=0; i<MAXPARALLELISM; i++) {
	qthread_readFF(NULL, NULL, rets+i);
    }
    qtimer_stop(timer);

    for (i=1; i<MAXPARALLELISM; i++) {
	total_roundtrip_time[0] += total_roundtrip_time[i];
	total_p1_sending_time[0] += total_p1_sending_time[i];
	total_p2_sending_time[0] += total_p2_sending_time[i];
    }
    printf("\tParallel FEB ping-pong loop: %15g secs (%u-way %u rts)\n", qtimer_secs(timer), MAXPARALLELISM, ITERATIONS);
    printf("\t - total rtts: %29g secs\n", total_roundtrip_time[0]);
    printf("\t - total sending time: %21g secs\n",
	   total_p1_sending_time[0] + total_p2_sending_time[0]);
    printf("\t + external avg rtt: %23g secs\n",
	   qtimer_secs(timer) / (MAXPARALLELISM*ITERATIONS));
    printf("\t + internal avg rtt: %23g secs\n",
	   total_roundtrip_time[0] / (MAXPARALLELISM*ITERATIONS));
    printf("\t + average p1 sending time: %16g secs\n",
	   total_p1_sending_time[0] / (MAXPARALLELISM*ITERATIONS));
    printf("\t + average p2 sending time: %16g secs\n",
	   total_p2_sending_time[0] / (MAXPARALLELISM*ITERATIONS));
    printf("\t + average sending time: %19g secs\n",
	   (total_p1_sending_time[0] +
	    total_p2_sending_time[0]) / (MAXPARALLELISM*ITERATIONS * 2));
    /* each rt is 2 messages, thus: */
    printf("\t = messaging throughput: %19g msgs/sec\n",
	   (MAXPARALLELISM*ITERATIONS * 2) / total_roundtrip_time[0]);
    /* each rt is 1 message of aligned_t bytes each, thus: */
    rate = (MAXPARALLELISM*ITERATIONS * sizeof(aligned_t)) / total_roundtrip_time[0];
    printf("\t = data roundtrip tput: %20g bytes/sec %s\n", rate,
	   human_readable_rate(rate));
    /* each send is 1 messsage of aligned_t bytes, thus: */
    rate = (MAXPARALLELISM*ITERATIONS * sizeof(aligned_t)) / total_p1_sending_time[0];
    printf("\t = p1 hop throughput: %22g bytes/sec %s\n", rate,
	   human_readable_rate(rate));
    rate = (MAXPARALLELISM*ITERATIONS * sizeof(aligned_t)) / total_p2_sending_time[0];
    printf("\t = p2 hop throughput: %22g bytes/sec %s\n", rate,
	   human_readable_rate(rate));
    rate =
	(MAXPARALLELISM*ITERATIONS * 2 * sizeof(aligned_t)) / (total_p1_sending_time[0] +
						total_p2_sending_time[0]);
    printf("\t = data hop throughput: %20g bytes/sec %s\n", rate,
	   human_readable_rate(rate));

    /* COMPETING INCREMENT LOOP */
    qtimer_start(timer);
    for (i = 0; i < MAXPARALLELISM; i++) {
	qthread_fork(incrloop, NULL, rets+i);
    }
    for (i = 0; i < MAXPARALLELISM; i++) {
	qthread_readFF(NULL, NULL, rets+i);
    }
    qtimer_stop(timer);
    assert(incrementme == ITERATIONS*MAXPARALLELISM);

    printf("\tcompeting increment loop: %18g secs\n", qtimer_secs(timer));
    printf("\t + average increment time: %17g secs\n",
	   qtimer_secs(timer) / (ITERATIONS*MAXPARALLELISM));
    printf("\t + increment speed: %'24f increments/sec\n",
	   (ITERATIONS*MAXPARALLELISM) / qtimer_secs(timer));
    rate = (ITERATIONS*MAXPARALLELISM * sizeof(aligned_t)) / qtimer_secs(timer);
    printf("\t = data throughput: %24g bytes/sec %s\n", rate,
	   human_readable_rate(rate));

    qthread_finalize();

    qtimer_free(timer);

    return 0;
}
