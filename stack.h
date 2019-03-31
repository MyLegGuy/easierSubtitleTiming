#ifndef HEADERSTACKINCLUDED
#define HEADERSTACKINCLUDED

typedef nList nStack;

void addStack(nStack** _passedStack, void* _data);
void* popStack(nStack** _passedStack);
char stackEmpty(nStack* _passedStack);
int sizeStack(nStack* _passedStack);

#endif