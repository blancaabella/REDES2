#include "demonizar.h"

int main(int argc, char const *argv[]){
	if(demonizar(NULL) < 0){
		return -1;	
	}
	return 0;
}

