#include "z80cpm.h"
#include "integer.h"

#include <stdlib.h>
#include <stdio.h>

#define PAGE_SIZ 1024
#define PAGE_NUM 8

#define PAGE_MASK (~(PAGE_SIZ - 1))

FILE *swap, *disk;

BYTE *page[PAGE_NUM];
WORD page_adr[PAGE_NUM];
DWORD page_time[PAGE_NUM];
int page_fifo[PAGE_NUM];

int eeprom_siz;
BYTE *eeprom;

int cpm_disk_initialize(BYTE n)
{
	return 0;
}

int cpm_disk_read(BYTE pdrv, BYTE* buff, DWORD sector, int count)
{
	FILE *fp = disk;//fopen("diskimage","rb");
	
	fseek(fp,sector*512,SEEK_SET);
	
	fread(buff,512,count,fp);
	
	//fclose(fp);
	
	return 0;
}

int cpm_disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, int count)
{
	FILE *fp = disk;//fopen("diskimage","r+b");
	
	fseek(fp,sector*512,SEEK_SET);
	
	fwrite(buff,512,count,fp);
	
	//fclose(fp);
	
	return 0;
}

int new_page()
{
	/*int lst_page = 0;
	int lst_time = 0;
	
	for(int i=0;i<PAGE_NUM;i++) {
		int ltim = clock() - page_time[i];
		if(ltim > lst_time) {
			lst_page = i;
			lst_time = ltim;
		}
	}*/
	
	int lst_page = page_fifo[0];
	
	for(int i=0;i<PAGE_NUM-1;i++) {
		page_fifo[i] = page_fifo[i+1];
	}
	
	page_fifo[PAGE_NUM-1] = lst_page;
	
	//printf("PAGE_IN [%d] : ", lst_page);
	
	return lst_page;
}

BYTE cpm_memory_read(WORD adr)
{
	for(int i=0;i<PAGE_NUM;i++) {
		if(page_adr[i] <= adr && page_adr[i] + PAGE_SIZ > adr) {
			//page_time[i] = clock();
			return page[i][adr - page_adr[i]];
		}
	}
	
	int newpage = new_page();
	int newpage_adr = adr & PAGE_MASK;
	
	//printf("%04X -> %04X(R)\n", page_adr[newpage], newpage_adr);
	
	page_time[newpage] = 0;
	
	FILE *fp = swap;//fopen("pagefile","r+b");
	
	fseek(fp,page_adr[newpage],SEEK_SET);
	fwrite(page[newpage],PAGE_SIZ,1,fp);
	
	fseek(fp,newpage_adr,SEEK_SET);
	fread(page[newpage],PAGE_SIZ,1,fp);
	
	//fclose(fp);
	
	page_adr[newpage] = newpage_adr;
	
	return page[newpage][adr - newpage_adr];
}

void cpm_memory_write(WORD adr, BYTE data)
{
	for(int i = 0; i < PAGE_NUM; i++) {
		if(page_adr[i] <= adr && page_adr[i] + PAGE_SIZ > adr) {
			//page_time[i] = clock();
			page[i][adr - page_adr[i]] = data;
			return;
		}
	}
	
	int newpage = new_page();
	int newpage_adr = adr & PAGE_MASK;
	
	//printf("%04X -> %04X(W)\n", page_adr[newpage], newpage_adr);
	
	page_time[newpage] = 0;
	
	FILE *fp = swap;//fopen("pagefile","r+b");
	
	fseek(fp,page_adr[newpage],SEEK_SET);
	fwrite(page[newpage],PAGE_SIZ,1,fp);
	
	fseek(fp,newpage_adr,SEEK_SET);
	fread(page[newpage],PAGE_SIZ,1,fp);
	
	//fclose(fp);
	
	page_adr[newpage] = newpage_adr;
	
	page[newpage][adr - newpage_adr] = data;
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
	
	for(int i = 0; i < PAGE_NUM; i++) {
		page[i] = (BYTE *)malloc(PAGE_SIZ);
		page_adr[i] = 0x0000 + PAGE_SIZ * i;
		page_time[i] = 0;
		page_fifo[i] = i;
	}
	
	swap = fopen("pagefile","r+b");
	disk = fopen("diskimage","r+b");
	
	cpm80_emulation();

	return 0;
}
