#include "iodefine.h"
#include "typedefine.h"
#include "math.h"
#include "machine.h"
#include "ctype.h"

#define	printf	((int (*)(const char *,...))0x00007c7c)
#define	scanf	((int (*)(const char *,...))0x00007cb8)
#define TFTDATA  (*(volatile unsigned short*)0x08000000)
#define TFTCTRL  (*(volatile unsigned short*)0x08000002)
#define _COL_WHITE    (0xFFFF)
#define _COL_RED      (0xF800)
#define _COL_GREEN    (0x07E0)
#define _COL_BLUE     (0x001F)
#define _COL_BLACK    (0x0000)

#define LCD_RS		(PA.DR.BIT.B22)
#define LCD_E		(PA.DR.BIT.B23)
#define LCD_RW		(PD.DR.BIT.B23)
#define LCD_DATA	(PD.DR.BYTE.HH)

#define SPK (PE.DR.BIT.B0)

#define YOKO	16
#define TATE	16

#define M_X 20
#define M_Y 15

#define  NUM 32

#define	SD_CD		(PE.DR.BIT.B11)
#define	SD_CS		(PE.DR.BIT.B9)

#define	DIG1	(PE.DR.BIT.B3)
#define	DIG2	(PE.DR.BIT.B2)
#define	DIG3	(PE.DR.BIT.B1)

#define SW6 (PD.DR.BIT.B18)
#define SW5 (PD.DR.BIT.B17)
#define SW4 (PD.DR.BIT.B16)

void TFT_On(void);
void TFT_clear(void);
void TFT_draw_screen(void);
void TFT_draw_point(_UWORD, _UWORD, _UWORD);
void TFT_put_img16(_UWORD, _UWORD, _UWORD*);
void seg_disp();

volatile	_UWORD FrameBuf[320*240];
unsigned char dt[512], fat[512], img[512];

_UWORD img_A[YOKO*TATE];
_UWORD img_B[YOKO*TATE];
_UWORD img_C[YOKO*TATE];
_UWORD img_D[YOKO*TATE];
_UWORD img_E[YOKO*TATE];
int meiro[M_Y][M_X];
int	num;
int status;
int clear;
int over;

enum STS{STOP, RUN, CLEAR, OVER, END};

struct position {
	int x;
	int y;
	int active;
};

struct position me;

// ------------------------------------------------------------
void init_CMT0()
{
	STB.CR4.BIT._CMT = 0;

	CMT0.CMCNT = 0;
	CMT0.CMCSR.BIT.CKS = 1;		// CKS設定(0:1/8, 1:1/32, 2:1/128, 3:1/512)
}


void wait_us(unsigned int us) {
	unsigned int val;

	val = us * 10 / 16;
	if (val >= 0xffff)
		val = 0xffff;

	CMT0.CMCOR = val;
	CMT0.CMCSR.BIT.CMF &= 0;
	CMT.CMSTR.BIT.STR0 = 1;

	while (CMT0.CMCSR.BIT.CMF == 0)
		;
	CMT0.CMCSR.BIT.CMF = 0;
	CMT.CMSTR.BIT.STR0 = 0;
}

/********************************************************/
/* LCD モジュールにインストラクションコードを書き込む */
/********************************************************/
void LCD_inst(char inst) {
	LCD_E = 0;
	LCD_RS = 0;
	LCD_RW = 0;
	LCD_E = 1;
	LCD_DATA = inst;
	wait_us(1);
	LCD_E = 0;
	wait_us(40);
}
/********************************************************/
/* LCD モジュールにデータを書き込む */
/********************************************************/
void LCD_data(char data) {
	LCD_E = 0;
	LCD_RS = 1;
	LCD_RW = 0;
	LCD_E = 1;
	LCD_DATA = data;
	wait_us(1);
	LCD_E = 0;
	wait_us(40);
}

/********************************************************/
void LCD_init() {
	wait_us(45000);
	LCD_inst(0x30);
	wait_us(4100);
	LCD_inst(0x30);
	wait_us(100);
	LCD_inst(0x30);
	
	LCD_inst(0x38);
	LCD_inst(0x08);
	LCD_inst(0x01);
	wait_us(1640);
	LCD_inst(0x06);
	LCD_inst(0x0c);
}

void LCD_cursor(unsigned int x, unsigned int y) {
	if (x > NUM - 1 || x < 0)
		x = NUM - 1;

	if (y > 1 || y < 0)
		y = 0;

	LCD_inst(0x80 | x | y << 6);
}
/********************************************************/
void LCD_putch(char ch) {
	LCD_data(ch);
}
/********************************************************/
void LCD_putstr(char *str) {
	char ch;

	while (ch = *str++)
		LCD_putch(ch);
}

/********************************************************/
/*LCDクリア命令, 1640us待ち*/
/********************************************************/
void LCD_cls() {
	LCD_inst(0x01);
	wait_us(1640);
}

// ------------------------------------------------------------
void init_SCI2()
{
	STB.CR3.BIT._SCI2 = 0;
	SCI2.SCSCR.BYTE= 0x00;
	SCI2.SCSMR.BIT.CA = 1;		// クロック同期式
	SCI2.SCBRR = 12;			// 384kbps
	SCI2.SCSDCR.BIT.DIR = 1;	// MSB first
	wait_us(1);
	PFC.PECRL3.BIT.PE10MD = 2;	// PE10 .. TxD
	PFC.PECRL3.BIT.PE8MD = 2;	// PE8 .. SCK
	PFC.PECRL2.BIT.PE7MD = 2;	// PE7 .. RxD
	PFC.PEIORL.BIT.B12 = 0;		// PE12 .. WP入力
	PFC.PEIORL.BIT.B11 = 0;		// PE11 .. CD入力
	PFC.PEIORL.BIT.B9 = 1;		// PE9 .. CS出力
	PE.DR.BIT.B9 = 1;			// CS初期値
	SCI2.SCSCR.BYTE |= 0x30;	// TE, RE = 1
}


unsigned char SPI_tx_rx(unsigned char ch)
{
	while (!SCI2.SCSSR.BIT.TDRE)
		;
	SCI2.SCTDR = ch;
	SCI2.SCSSR.BIT.TDRE = 0;

	while (!SCI2.SCSSR.BIT.RDRF)
		;
	ch = SCI2.SCRDR;
	SCI2.SCSSR.BIT.RDRF = 0;
	return (ch);
}


// ------------------------------------------------------------
int card_exist()
{
	return (PE.DR.BIT.B11 ? 0 : 1);
}


unsigned char calc_CRC7(unsigned char *data, int len)
{
	int i, j;
	char crc, dt;

	crc = 0;
	for (i = 0; i < len; i++) {
		dt = *data++;
		for (j = 0; j < 8; j++) {
			crc <<=1;
			if ((crc & 0x80) ^ (dt & 0x80))
				crc ^= 0x09;
			dt <<= 1;
		}
	}
	return (crc & 0x7f);
}


unsigned char SD_send_cmd(unsigned char cmd, int arg)
{
	int i;
	unsigned char cmd_token[6], ret;

	while (1) {
		if (SPI_tx_rx(0xff) == 0xff)
			break;
	}

	cmd_token[0] = cmd;
	cmd_token[1] = (arg >> 24) & 0xff;
	cmd_token[2] = (arg >> 16) & 0xff;
	cmd_token[3] = (arg >> 8) & 0xff;
	cmd_token[4] = arg & 0xff;
	cmd_token[5] = (calc_CRC7(cmd_token, 5) << 1) | 0x01;

	for (i = 0; i < 6; i++)
		SPI_tx_rx(cmd_token[i]);

	do {
		ret = SPI_tx_rx(0xff);
	} while (ret & 0x80);

	return (ret);
}


int Enter_SPI_mode()
{
	int i;
	unsigned char ret;

	SD_CS = 1;				// CS negate

	for (i = 0; i < 10; i++)
		SPI_tx_rx(0xff);

	SD_CS = 0;				// CS assert

	ret = SD_send_cmd(0x40, 0);
	if (ret != 0x01)
		return (-1);

	do {
		ret = SD_send_cmd(0x41, 0);
	} while (ret != 0x00);

	return (1);
}


void read_sector(unsigned int sect, unsigned char *dt)
{
	unsigned char ch;
	int i;

	ch = SD_send_cmd(0x51, sect * 512);
	while (ch != 0xfe)
		ch = SPI_tx_rx(0xff);

	for (i = 0; i < 512; i++)
		*dt++ = SPI_tx_rx(0xff);
	SPI_tx_rx(0xff);				// CRC分
	SPI_tx_rx(0xff);				// CRC分
}


void print_sector(unsigned int sect, unsigned char *dt)
{
	int i, j;
	unsigned int adr;

	adr = sect * 512;

	printf("           ");
	for (i = 0; i < 16; i++) {
		printf("+%x ", i);
		if (i == 7)
			printf(" ");
	}
	printf("\n");

	for (i = 0; i < 32; i++) {
		printf("%08x : ", adr);
		for (j = 0; j < 16; j++) {
			printf("%02x ", dt[i * 16 + j]);
			if (j == 7)
				printf(" ");
		}
		printf(" ");
		for (j = 0; j < 16; j++) {
			if (isprint(dt[i * 16 + j]))
				printf("%c", dt[i * 16 + j]);
			else
				printf(".");
			if (j == 7)
				printf(" ");
		}
		printf("\n");
		adr += 16;
	}

}

void TFT_draw_screen(void) {
  _SINT i;

  TFTCTRL = 0x4001;
  for (i = 0; i < (320*240); i++ ) {
    TFTDATA = FrameBuf[i];
  }
}

void TFT_clear(void) {
  _SINT i;

  TFTCTRL = 0x4001;
  for (i = 0; i < (320*240); i++ ) {
    TFTDATA = _COL_WHITE;
    FrameBuf[i] = _COL_WHITE;
  }
}

void TFT_On(void) {
  TFTCTRL = 0x4000;
}

void init_CS2(void) {
  BSC.CS2BCR.LONG = 0x12490400;
  BSC.CS2WCR      = 0x000302C0;
  PFC.PACRL4.BIT.PA15MD = 1;
  PFC.PACRL2.BIT.PA6MD  = 2;
}

void TFT_draw_point(_UWORD x_pix, _UWORD y_pix, _UWORD p_color) {
	if(p_color != 0x0000)
		FrameBuf[y_pix*320+x_pix] = p_color;
}

void TFT_put_img16(_UWORD x, _UWORD y, _UWORD *img) {
	_UWORD i, j;
	for(j = 0; j< TATE; j++){
		for(i = 0; i< YOKO; i++){
			TFT_draw_point(x + i, y + j, *img++);
		}
	}
}

//ファイル読み込みする関数
void readpic(unsigned int offset, _UWORD *img){
	int i, j, k = 0;
	unsigned int First_sect_LBA, First_RDE_sect, First_FAT_sect, First_Data_sect;
	unsigned int BPB_BytesPerSec, BPB_SecPerClus, BPB_RsvdSecCnt;
	unsigned int BPB_NumFATs, BPB_RootEntCnt, BPB_FATSz16;
	unsigned int File_year, File_month, File_date, File_hour, File_min, File_sec;
	unsigned int FstClusLO, FileSize;
	unsigned int cluster;

	SD_send_cmd(0x50, 512);		// ブロックサイズ=512

	// MBR
	read_sector(0, dt);
	First_sect_LBA = (dt[457] << 24) | (dt[456] << 16) | (dt[455] << 8) | dt[454]; 

	// BPB
	read_sector(First_sect_LBA, dt);
	BPB_BytesPerSec = (dt[12] << 8) | dt[11];
	BPB_SecPerClus = dt[13];
	BPB_RsvdSecCnt = (dt[15] << 8) | dt[14];
	BPB_NumFATs = dt[16];
	BPB_RootEntCnt = (dt[18] << 8) | dt[17];
	BPB_FATSz16 = (dt[23] << 8) | dt[22];
	
	
	//RDE
	First_RDE_sect = First_sect_LBA + BPB_RsvdSecCnt + (BPB_FATSz16 * BPB_NumFATs);
	First_Data_sect = First_RDE_sect + ((32 * BPB_RootEntCnt) / BPB_BytesPerSec);
	read_sector(First_RDE_sect, dt);
    FstClusLO = (dt[offset + 27] << 8) | dt[offset + 26];			
	FileSize = (dt[offset + 31] << 24) | (dt[offset + 30] << 16) | (dt[offset + 29] << 8) | dt[offset + 28];
	
	
	//FAT(EX4)
	//ファイル読み込み
	First_FAT_sect = First_sect_LBA + BPB_RsvdSecCnt;
	First_Data_sect = First_RDE_sect + (32 * BPB_RootEntCnt) / BPB_BytesPerSec;
	read_sector(First_FAT_sect, fat);
	cluster = FstClusLO;
	while(cluster != 0xffff){
		for(i = 0;i < BPB_SecPerClus;i++){
			read_sector((cluster - 2) * BPB_SecPerClus + First_Data_sect + i, dt);
			for(j = 0;j < BPB_BytesPerSec;j+=2){
				img[k] = (dt[j] << 8) | (dt[j + 1]);	//ビットパターンを格納
				k++;
				if(j >= FileSize){
					FileSize = 0;
					break;
				}
			}
			if(FileSize == 0)
				break;
			FileSize -= BPB_BytesPerSec;
		}
		if(FileSize == 0)
			break;
		cluster = (fat[cluster * 2 + 1] << 8) | fat[cluster * 2];
	}
}

struct music_data_t {
	int tgra; //音の周波数用
	int len; //音の長さ(ms 単位)
};

void play_sound(int i){	//音を出す
	int TGRA_old;
	int j;
	struct music_data_t base_music[] = {
		20243, 500, //シ:494Hz
		19120, 500, //ド:523Hz
		18051, 500, //ド#:554Hz
		17036, 500, //レ:587Hz
		16077, 500, //レ#:622Hz
		15175, 500, //ミ:659Hz
		14327, 500, //ファ:698Hz
		13514, 500, //ファ#:740Hz
		12755, 500, //ソ:392Hz
		12033, 500, //ソ#:415Hz
		11364, 500, //ラ:440Hz
		10730, 500, //ラ#:466Hz
		10121, 500, //シ:494Hz
		9569, 500, //ド:523Hz
		0xffff, 0xffff
	};

	TGRA_old = MTU21.TGRA;

	//MTU2 ch2 .. 音の長さ用
	MTU22.TCR.BIT.TPSC = 0; // 1/1選択
	MTU22.TCR.BIT.CCLR = 1; // TGRA のコンペアマッチでクリア
	MTU22.TGRA = 20000 - 1; // 1ms

	MTU2.TSTR.BIT.CST2 = 1; // MTU2 CH4 スタート

	MTU21.TGRA = base_music[i].tgra;
	MTU2.TSTR.BIT.CST1 = 1; // MTU2 CH3 スタート
	for (j = 0;j < base_music[i].len;j++){
		while(!MTU22.TSR.BIT.TGFA) {
		if (MTU21.TSR.BIT.TGFA) {
			MTU21.TSR.BIT.TGFA = 0;
			SPK ^= 1;
		}
		}
		MTU22.TSR.BIT.TGFA = 0;
	}
	MTU22.TSR.BIT.TGFA = 0;

	MTU2.TSTR.BIT.CST1 = 0; // MTU2 CH3 ストップ
	MTU2.TSTR.BIT.CST2 = 0; // MTU2 CH4 ストップ

	MTU21.TGRA = TGRA_old;
}

void move_me(){	//プレーヤーの移動
	static int n2;
	if(n2 == 1){
		struct position new_position;
		
		n2 = 0;

		new_position.x = me.x;
		new_position.y = me.y;

		//上下
		if (AD0.ADDR0 < 0x4000) { // -- ジョイスティック上--
			new_position.y -= 1;
		} else if (AD0.ADDR0 > 0xc000) { // -- ジョイスティック下--
			new_position.y += 1;
		} else if(AD0.ADDR1 < 0x4000) { // -- ジョイスティック右--
			new_position.x +=1;
		} else if (AD0.ADDR1 > 0xc000) { // -- ジョイスティック左--
			new_position.x -= 1;
		}
	
		if(meiro[new_position.y][new_position.x] != 1){
			if(meiro[me.y][me.x] == 1){
					TFT_put_img16(me.x * TATE, me.y * YOKO, img_B);	//画像Bの表示
				}
				else if(meiro[me.y][me.x] == 2){
					TFT_put_img16(me.x * TATE, me.y * YOKO, img_C);	//画像Cの表示
				}
				else if(meiro[me.y][me.x] == 3){
					TFT_put_img16(me.x * TATE, me.y * YOKO, img_D);	//画像Dの表示
				}
	            else{
					TFT_put_img16(me.x * TATE, me.y * YOKO, img_A);	//画像Aの表示
				}
			me.x = new_position.x;
			me.y = new_position.y;
		}

		TFT_put_img16(me.x * YOKO, me.y * TATE, img_E);	//画像Aの表示

		TFT_draw_screen();
		if(meiro[me.y][me.x] == 3){	//クリアしたとき
			status = CLEAR;
		}
	}else{
		n2++;
	}
}

// -------------------------------------
// CMT0の割り込み処理関数
#pragma interrupt INT_CMT1_CMI1
void INT_CMT1_CMI1()
{	
	static int n;
	CMT1.CMCSR.BIT.CMF = 0;		// 割り込み要求をクリア
	
	if(status == RUN){	//動作中の時
		if(n >= 1000){	//1秒
			num--;	//カウントダウン
			n = 0;
		}
	}
	
	if(n % 5 == 0)	//カウントダウン表示
		seg_disp();
	
	n++;
}

// -------------------------------------

void initarray(){	//迷路の配列初期化
	int i, j;
    for(i = 0;i < M_Y;i++){
        for(j = 0;j < M_X;j++){
            if(i == 0 || i == M_Y - 1){
                meiro[i][j] = 1;
            }else if(j == 0 || j >= M_X - 2){
                meiro[i][j] = 1;
            }else if(i % 2 == 0 && j % 2 == 0){
                meiro[i][j] = 1;
            }else{
                meiro[i][j] = 0;
            }
        }
    }
    meiro[1][1] = 2;	//スタート
    meiro[M_Y - 2][M_X - 3] = 3;	//ゴール
}

int check_brock(int i, int j, int num){	//ブロックの判定
    int value = 0;
    int movex = 0;
    int movey = 0;
    switch(num) {
        case 0:	//上方向
            movey = -1;
            break;
        case 1:	//右方向
            movex = 1;
            break;
        case 2:	//下方向
            movey = 1;
            break;
        case 3:	//左方向
            movex = -1;
            break;
        default:
            break;
    }

    if(meiro[i + movey][j + movex] == 1){
        value = -1;	//ブロックがかぶったとき
    } else{
        meiro[i + movey][j + movex] = 1;	//ブロックを置けたとき
    }
    
    return value;
}

void botaosi(){	//棒倒し法の実行
    int num = 0;
    int error = 0;
	int i, j;
    for(i = 2;i < M_Y - 1;i += 2){
        for(j = 2;j < M_X - 3;j += 2){
            error = -1;
            while(error == -1){
                num = 0;
                if(i == 2){
                    num = rand() % 4;
                }else{
                    while(num == 0)
                        num = rand() % 4;
                }
                error = check_brock(i, j, num);
            }
        }
    }
}

void makemeiro(){	//迷路の表示
	int i, j;
	initarray();
	me.x = 1;
	me.y = 1;
	botaosi();
	//TFT表示
	for(i = 0;i < M_Y;i++){
        for(j = 0;j < M_X;j++){
            if(meiro[i][j] == 1){
				TFT_put_img16(j * TATE, i * YOKO, img_B);	//画像Bの表示
			}
			else if(meiro[i][j] == 2){
				TFT_put_img16(j * TATE, i * YOKO, img_C);	//画像Cの表示
			}
			else if(meiro[i][j] == 3){
				TFT_put_img16(j * TATE, i * YOKO, img_D);	//画像Dの表示
			}
            else{
				TFT_put_img16(j * TATE, i * YOKO, img_A);	//画像Aの表示
			}
        }
    }
 	
	TFT_put_img16(me.x * YOKO, me.y * TATE, img_E);	//画像Aの表示

	TFT_draw_screen();
}

void printstr(){	//LCD表示
	LCD_cursor(0, 0);
	LCD_putstr("game clear:");	//クリア回数
	LCD_putch('0' + clear / 10);
	LCD_putch('0' + clear % 10);
	LCD_cursor(0, 1);
	LCD_putstr("game over:");	//ゲームオーバー回数
	LCD_putch('0' + over / 10);
	LCD_putch('0' + over % 10);
}

// IRQ0(SW4)の割り込み処理関数
#pragma interrupt INT_IRQ0
void INT_IRQ0()
{
	INTC.IRQSR.BIT.IRQ1F = 0;		// 割り込み要求をクリア
	makemeiro();	//迷路を作る
	num = 20;		//時間を初期化
	status = STOP;	//STOP状態にする
}

void seg_disp(){	//7セグの表示
	static int keta;
		
	if (++keta >= 3)
		keta = 0;
		
	if(num < 0){
		num = 0;
		status = OVER;
	}
	if(num > 999)
		num = 999;
	DIG1 = DIG2 = DIG3 = 0;		// 全桁消灯
	PA.DR.BYTE.HL &= 0xf0;
	switch (keta) {
		case 0 :
			// 最下位桁
			PA.DR.BYTE.HL |= num % 10;
			DIG1 = 1;
			break;
		case 1 :
			PA.DR.BYTE.HL |= (num % 100) / 10;
			DIG2 = 1;
			break;
		default :
			// 最上位桁
			PA.DR.BYTE.HL |= num / 100;
			DIG3 = 1;
			break;
	}
}

// IRQ1(SW5)の割り込み処理関数
#pragma interrupt INT_IRQ1
void INT_IRQ1()
{
	INTC.IRQSR.BIT.IRQ1F = 0;		// 割り込み要求をクリア
	if(status == RUN)	
		status = STOP;
}

// IRQ2(SW6)の割り込み処理関数
#pragma interrupt INT_IRQ2
void INT_IRQ2()
{
	INTC.IRQSR.BIT.IRQ2F = 0;		// 割り込み要求をクリア
	if(status == STOP)	
		status = RUN;	//動作開始
}



// ------------------------------------------------------------
void main()
{
	
	PFC.PAIORH.BIT.B22 = 1;
	PFC.PAIORH.BIT.B23 = 1;
	PFC.PDIORH.BIT.B23 = 1;
	PFC.PDIORH.BYTE.H = 0xff;
	
	init_CS2();
	init_CMT0();
	init_SCI2();
	TFT_On();
	TFT_clear();
	
	LCD_init();
	LCD_cls();
	
	PFC.PAIORH.BYTE.L |= 0x0f;		//
	PFC.PEIORL.BYTE.L |= 0x0e;		// DIG1～DIG3を出力モードに設定
	
	// MTU2 ch0 //100ms 毎にAD 変換
	STB.CR4.BIT._MTU2 = 0;
	MTU20.TCR.BIT.TPSC = 3; // 1/64選択
	MTU20.TCR.BIT.CCLR = 1; // TGRA のコンペアマッチでクリア
	MTU20.TGRA = 31250 - 1; // 100ms
	MTU20.TIER.BIT.TTGE = 1; // A/D 変換開始要求を許可
	
	// MTU2 ch3...音用
	MTU21.TCR.BIT.TPSC = 0; // 1/1選択
	MTU21.TCR.BIT.CCLR = 1; // TGRA のコンペアマッチでクリア
	MTU21.TGRA = 10000 - 1; // 500ms

	// AD0
	STB.CR4.BIT._AD0 = 0;
	AD0.ADCSR.BIT.ADM = 3; // 2チャネルスキャンモード
	AD0.ADCSR.BIT.CH = 1; // AN0, AN1を変換
	AD0.ADCSR.BIT.TRGE = 1; // MTU2 からのトリガ有効
	AD0.ADTSR.BIT.TRG0S = 1; // TGRA コンペアマッチでトリガ

	STB.CR4.BIT._CMT = 0;			// CMTモジュールスタンバイの解除
	CMT1.CMCSR.BIT.CKS = 0;			// 1/8
	CMT1.CMCOR = 2500 - 1;			// 1ms
	CMT1.CMCSR.BIT.CMIE = 1;		// コンペアマッチ割り込み許可
	INTC.IPRJ.BIT._CMT1 = 9;		// 割り込み優先レベル = 9
	
	PFC.PDCRH1.BIT.PD16MD = 2;		// PD16(SW4)端子をIRQ0入力に設定
	INTC.IRQCR.BIT.IRQ0S = 2;		// 立ち上がりエッジ
	INTC.IPRA.BIT._IRQ0 = 8;		// 割り込み優先レベル = 8
	
	PFC.PDCRH1.BIT.PD17MD = 2;		// PD17(SW5)端子をIRQ1入力に設定
	INTC.IRQCR.BIT.IRQ1S = 2;		// 立ち上がりエッジ
	INTC.IPRA.BIT._IRQ1 = 8;		// 割り込み優先レベル = 8
	
	PFC.PDCRH1.BIT.PD18MD = 2;		// PD18(SW6)端子をIRQ2入力に設定
	INTC.IRQCR.BIT.IRQ2S = 2;		// 立ち上がりエッジ
	INTC.IPRA.BIT._IRQ2 = 8;		// 割り込み優先レベル = 8
	
	set_imask(7);					// マスクビット = 7
		
	if (!card_exist())
		printf("No card found\n");
	else {
		if (Enter_SPI_mode() < 0)
			printf("SPI mode Err\n");
		else {
			readpic(32 * 3, img_A);//画像Aの読み込み
			readpic(32 * 4, img_B);//画像Bの読み込み
			readpic(32 * 5, img_C);//画像Cの読み込み
			readpic(32 * 6, img_D);//画像Dの読み込み
			readpic(32 * 7, img_E);//画像Eの読み込み
		}
	}
	SD_CS = 1;				// CS negate
	
	printstr();
	makemeiro();
	num = 20;
	status = STOP;
	clear = 0;
	over = 0;
	MTU2.TSTR.BIT.CST0 = 1; // MTU2 CH0 スタート
	CMT.CMSTR.BIT.STR1 = 1;			// CMT1スタート
	while (1){
		if(status == RUN && MTU20.TSR.BIT.TGFA){
			move_me();
			MTU20.TSR.BIT.TGFA = 0;
		}
		if(status == CLEAR){
			clear++;
			play_sound(8);	//ソ
			printstr();
			status = END;
		}
		if(status == OVER){
			over++;
			play_sound(6);	//ファ
			printstr();
			status = END;
		}
	}
}
