/* Practice: self-registering test cases (constructor runs before main) */
#ifndef REGISTRY_H
#define REGISTRY_H

typedef int (*test_fn)(void);
void test_register(const char *name, test_fn fn);
#endif
