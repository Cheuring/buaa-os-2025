#include <stdio.h>
#include <string.h>
int isPalindrome(int n){
	if(n < 0){
		return 0;
	}

	char num[10] = {0};
	snprintf(num, sizeof(num), "%d", n);

	int j = strlen(num) - 1;
	int i=0;
	while(i < j){
		if(num[i] != num[j]){
			return 0;
		}

		++i, --j;
	}

	return 1;
}
int main() {
	int n;
	scanf("%d", &n);

	if (isPalindrome(n)) {
		printf("Y\n");
	} else {
		printf("N\n");
	}
	return 0;
}
