#include <stdio.h>
#include <unistd.h>
int main(int argc, char **argv)
{
	FILE *fp;
	int ch;
	fp = fopen(argv[1], "r");
	while((ch = fgetc(fp)) != EOF)
	{
		fputc(ch, stdout);
		fflush(stdout);
		usleep(100000);
	}
	fclose(fp);
	return 0;
}
