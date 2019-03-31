#include <stdlib.h>

#include "goodLinkedList.h"
#include "stack.h"

void addStack(nStack** _passedStack, void* _data){
	nStack* _newEntry = malloc(sizeof(nStack));
	_newEntry->data = _data;
	_newEntry->nextEntry = *_passedStack;
	*_passedStack=_newEntry;
}
void* popStack(nStack** _passedStack){
	void* _ret = (*_passedStack)->data;
	nStack* _newStart = (*_passedStack)->nextEntry;
	free(*_passedStack);
	*_passedStack = _newStart;
	return _ret;
}
char stackEmpty(nStack* _passedStack){
	return (_passedStack==NULL);
}
int sizeStack(nStack* _passedStack){
	return nListLen(_passedStack);
}