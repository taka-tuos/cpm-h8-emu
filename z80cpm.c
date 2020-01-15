/*----------------------------------------------------------------------*/
/* CP/M system on Z80CPU and AVR                           2011.7.11    */
/*                                for mega644P Fuse L:H:X = E6:D1:FF    */
/*                                                                      */
/*                                                      (C)neko Java    */
/*----------------------------------------------------------------------*/

#include <string.h>

#include "z80cpm.h"
#include "integer.h"
#include "z80emu.h"

#include "xprintf.h"

unsigned char z80_rd, z80_wr;

/*---------------------------------------------------------*/
/* enviroments depend                                      */
/*---------------------------------------------------------*/

extern int eeprom_siz;
extern BYTE *eeprom;

#define RES_OK 0
#define STA_NOINIT 1

int cpm_disk_initialize(BYTE pdrv);
int cpm_disk_read(BYTE pdrv, BYTE* buff, DWORD sector, int count);
int cpm_disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, int count);

BYTE cpm_serial_read();
void cpm_serial_write(BYTE d);
int cpm_serial_available();

extern BYTE cpm_memory_read(WORD addr);
extern void cpm_memory_write(WORD addr, BYTE data);

/*---------------------------------------------------------*/
/* global variables                                        */
/*---------------------------------------------------------*/

#ifndef BLKBUF_EXTRAM
BYTE Block_buff[BLOCK_SIZE];	/* SDC block buffer */
#else
BYTE *Block_buff = (BYTE *)BLKBUF_EXTRAM;
#endif

volatile BYTE DMA_mode;		/* READ or WRITE or result */
volatile BYTE Adrl;
volatile BYTE Adrh;
volatile WORD Adr;
volatile BYTE Sect_no;
volatile BYTE Track_no_l;
volatile BYTE Track_no_h;
volatile WORD Track_no;
volatile DWORD Block_no;
volatile DWORD Pre_block_no;
volatile DWORD Sdc_clst_no;
volatile BYTE Buff_offset;

volatile BYTE F_unwritten;
volatile DWORD Pre_sdc_clst_no;
volatile WORD Write_back_timer;

volatile int St;

/*---------------------------------------------------------*/

void write_byte(WORD adr, BYTE dat)
{
	//*((BYTE *)memory + adr) = dat;
	cpm_memory_write(adr, dat);
	return;
}


BYTE read_byte(WORD adr)
{
	return cpm_memory_read(adr);//*((BYTE *)memory + adr);
}


void ram_write(WORD adr, WORD size, BYTE *buff, BYTE offset)	// AVR buffer -> Z80 RAM
{
	WORD i;

	for(i=0; i<size; i++){
		write_byte(adr+i, buff[i+offset*SECT_SIZE]);
	}

}


void ram_read(WORD adr, WORD size, BYTE *buff, BYTE offset)	// AVR buffer <- Z80 RAM
{
	WORD i;

	for(i=0; i<size; i++){
		buff[i+offset*SECT_SIZE] = read_byte(adr+i);
	}
}


void var_calc() // recalculation global variables 
{
	DWORD cpm_clst_no = 0;

	Adr = ((WORD)Adrh << 8) + (WORD)Adrl;

	//Track_no_h = 0;
	Track_no = ((WORD)Track_no_h << 8) + (WORD)Track_no_l;

	Pre_block_no = Block_no;
	Pre_sdc_clst_no = Sdc_clst_no;

	cpm_clst_no = (DWORD)(Sect_no + Track_no * SECT_CNT);	//CPMの128バイト単位で見たクラスタNo.
	Block_no = cpm_clst_no / CPM_CLST_CNT_PER_BLOCK;	//SDCの書込単位でのブロックNo.
	Buff_offset = cpm_clst_no % CPM_CLST_CNT_PER_BLOCK;	// 0〜7(1ブロック1024バイト、1セクタ128バイトの場合)

	Sdc_clst_no = Block_no * SDC_CLST_CNT_PER_BLOCK;	//SDCのクラスタNo.(1クラスタ512バイト)
}


void sdc_write()
{	// Z80 RAM -> AVR buffer -> SDC
	int rs;
	var_calc();
	if(Block_no != Pre_block_no){	//ブロックを移動した場合
		if(F_unwritten){	//未書込みの場合まず前のブロックをSDCへ書込んでから
			rs = cpm_disk_write(0, Block_buff, Pre_sdc_clst_no, SDC_CLST_CNT_PER_BLOCK);
			if(rs != RES_OK){
				DMA_mode = DMA_NG;
				St = STA_NOINIT;
				return;
			}
		}
		rs = cpm_disk_read(0, Block_buff, Sdc_clst_no, SDC_CLST_CNT_PER_BLOCK);	//SDCからバッファへ読込み
		if(rs != RES_OK){
			DMA_mode = DMA_NG;
			St = STA_NOINIT;
			return;
		}
	}
	ram_read(Adr, SECT_SIZE, Block_buff, Buff_offset);
	DMA_mode = DMA_OK;
	F_unwritten = 1;	//未書込みフラグをたてる
	Write_back_timer = 0;	//タイマをリセット
}


void sdc_read()
{	// Z80 RAM <- AVR buffer <- SDC
	int rs;
	var_calc();
	if(Block_no != Pre_block_no){	//ブロックを移動した場合
		if(F_unwritten){	//未書込みの場合まず前のブロックをSDCへ書込んでから
			rs = cpm_disk_write(0, Block_buff, Pre_sdc_clst_no, SDC_CLST_CNT_PER_BLOCK);
			if(rs != RES_OK){
				DMA_mode = DMA_NG;
				St = STA_NOINIT;
				return;
			}
			F_unwritten = 0;	//未書込みフラグをおろす
		}
		rs = cpm_disk_read(0, Block_buff, Sdc_clst_no, SDC_CLST_CNT_PER_BLOCK);	//SDCからバッファへ読込み
		if(rs != RES_OK){
			DMA_mode = DMA_NG;
			St = STA_NOINIT;
			return;
		}
		/*xputs("SDC DUMP :\n");
		
		for(int i = 0; i < SDC_CLST_CNT_PER_BLOCK * SDC_CLST_SIZE; i++) {
			xprintf("%02x ", Block_buff[i]);
			if((i & 15) == 15) xputs("\n");
		}*/
	}
	ram_write(Adr, SECT_SIZE, Block_buff, Buff_offset);
	DMA_mode = DMA_OK;
}


void rom_load()
{	// AVR EEPROM -> Z80 RAM (for Z80 MONITOR program)

	BYTE count, i;
	WORD size;

	if(EEPROM_SIZE >= BLOCK_SIZE){
		count = EEPROM_SIZE/BLOCK_SIZE;
		size = BLOCK_SIZE;
	}else{
		count = 1;
		size = EEPROM_SIZE;
	}
	for(i=0; i<count; i++){
		memcpy(Block_buff, (eeprom + i*size), size); 
		ram_write(i*size, size, Block_buff, 0);
	}
	Block_no = 0xFFFF; //ROMのプログラムがSDCを読む場合に備えて適当にずらしておく
	Sdc_clst_no = Block_no * SDC_CLST_CNT_PER_BLOCK;
	F_unwritten = 0;
}

void reset_proc()
{	// SDC first sector -> Z80 RAM and reset

	int rs;

	Block_no = 0;
	Sdc_clst_no = 0;
	Track_no_h= 0;
	F_unwritten = 0;

	St = cpm_disk_initialize(0);

	if(St & STA_NOINIT){	// SDC読込みに失敗した場合はEEPROMからモニタプログラムをロードする
		xputs("SDC INIT NG\n");
		rom_load();
	} else {
		xputs("SDC INIT OK\n");
		rs = cpm_disk_read(0, Block_buff, Block_no, 1);	// get boot loader from SDC
		xprintf("MBR READ %s\n", rs==RES_OK ? "OK" : "NG");
		if(rs==RES_OK){
			ram_write(0x2000, SECT_SIZE, Block_buff, 0); // set boot loader to Z80 RAM
			
			xputs("MBR DUMP :\n");
			
			for(int i = 0; i < SECT_SIZE; i++) {
				xprintf("%02x ", Block_buff[i]);
				if((i & 15) == 15) xputs("\n");
			}
			
			Block_buff[0]=0xC3;	// jump vector set.
			Block_buff[1]=0x00;
			Block_buff[2]=0x20;
			ram_write(0, 3, Block_buff, 0);
		}else{
			rom_load();
		}

	}

}

void write_back()
{	//ライトバック
	int rs;

	rs = cpm_disk_write(0, Block_buff, Sdc_clst_no, SDC_CLST_CNT_PER_BLOCK);
	if(rs != RES_OK){
		DMA_mode = DMA_NG;
		St = STA_NOINIT;
		return;
	}
	DMA_mode = DMA_OK;
	F_unwritten = 0;	//未書込みフラグをおろす
	Write_back_timer = 0;	//タイマをリセット
}

void write_back_chk()
{
	if(DMA_mode != DMA_WRITE_BACK) {
		if(F_unwritten) {
			Write_back_timer++;
			if(Write_back_timer > 100){
				DMA_mode = DMA_WRITE_BACK;
				busak_proc();
			}
		}
	}
}

void busak_proc()
{
	if(DMA_mode == DMA_WRITE_BACK){
		write_back();
	}
	if(DMA_mode == DMA_WRITE){
		sdc_write();
	}
	if(DMA_mode == DMA_READ){
		sdc_read();
	}
	
	write_back_chk();
}

int kbhit();
int getch();

unsigned char ioreq_proc(BYTE port_adr, BYTE port_data)
{
	if(RD()==0 && WR()!=0){		// AVR port -> Z80
		write_back_chk();
		switch (port_adr) {
		case CON_STS:
			if(cpm_serial_available()){
				return 0xFF;	// data exist
			}else{
				return 0;	// no data
			}
			break;
		case CON_IN:
			return cpm_serial_read();
			break;
		case DMA_RS:
			return DMA_mode;	// set DMA result
		}
	}
	if(WR()==0 && RD()!=0){		// AVR port <- Z80
		switch (port_adr) {
		case CON_OUT:
			cpm_serial_write(port_data);
			break;
		case TRACK_SEL_L:
			Track_no_l = port_data;
			break;
		case TRACK_SEL_H:
			Track_no_h = port_data;
			break;
		case SECTOR_SEL:
			Sect_no = port_data;
			break;
		case ADR_L:
			Adrl = port_data;
			break;
		case ADR_H:
			Adrh = port_data;
			break;
		case EXEC_DMA:
			DMA_mode = port_data;	// get read or write command
			if(St & STA_NOINIT){
				St = cpm_disk_initialize(0);
				if(St & STA_NOINIT){
					DMA_mode = DMA_NG;
					break;
				}
			}
			busak_proc();
		}
		write_back_chk();
	}
	
	return 0;
}

/*-----------------------------------------------------------------------*/
/* Main                                                                  */
/*-----------------------------------------------------------------------*/

int cpm80_emulation(void)
{
	xdev_in(cpm_serial_read);
	xdev_out(cpm_serial_write);
	
	Z80_STATE z80;
	
	Z80Reset(&z80);

	reset_proc(); 	// store boot-loader to Z80 RAM.

	while(1) {
		Z80Emulate(&z80, 1000, NULL);
	}
}
