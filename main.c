#include "z80cpm.h"
#include "integer.h"

BYTE *memory;
int eeprom_siz;
BYTE *eeprom;

#include <stdlib.h>
#include <stdio.h>

int cpm_disk_initialize(BYTE n)
{
	return 0;
}

int cpm_disk_read(BYTE pdrv, BYTE* buff, DWORD sector, int count)
{
	FILE *fp = fopen("diskimage","rb");
	
	fseek(fp,sector*512,SEEK_SET);
	
	fread(buff,512,count,fp);
	
	fclose(fp);
	
	return 0;
}

int cpm_disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, int count)
{
	FILE *fp = fopen("diskimage","r+b");
	
	fseek(fp,sector*512,SEEK_SET);
	
	fwrite(buff,512,count,fp);
	
	fclose(fp);
	
	return 0;
}

#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

int g_getch()
{
	struct termios oldt, newt;
	int ch;
	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	ch = getchar();
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	return ch;
}

int g_kbhit(void)
{
	struct termios oldt, newt;
	int ch;
	int oldf;

	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

	ch = getchar();

	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	fcntl(STDIN_FILENO, F_SETFL, oldf);

	if(ch != EOF)
	{
	ungetc(ch, stdin);
	return 1;
	}

	return 0;
}

BYTE cpm_serial_read()
{
	int c = g_getch();
	if(c == 0x7f) c = 0x08;
	return c;
}

void cpm_serial_write(BYTE d)
{
	putchar(d);
}


int cpm_serial_available()
{
	return g_kbhit();
}

int main(void)
{
	FILE *fp=fopen("eeprom.bin","rb");
	
	fseek(fp,0,SEEK_END);
	eeprom_siz = ftell(fp);
	fseek(fp,0,SEEK_SET);
	
	eeprom = (BYTE *)malloc(eeprom_siz);
	
	fread(eeprom,eeprom_siz,1,fp);
	
	fclose(fp);
	
	memory = (BYTE *)malloc(65536);
	
	cpm80_emulation();

	return 0;
}
