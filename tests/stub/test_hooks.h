/* Hooks the game calls when built with -DSM_HOST_TEST. */
#ifndef TEST_HOOKS_H
#define TEST_HOOKS_H
void test_present(int screen);   /* a frame is about to be swapped in; screen id */
void test_trunc(void);           /* a string buffer was too small */
void test_del_quit(void);        /* DEL was accepted: the program is about to quit */
#endif
