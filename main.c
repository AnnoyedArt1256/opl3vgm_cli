#include <stdio.h>
#include <stdint.h>

#include "SDL.h"
#include "SDL_endian.h"
#include "SDL_audio.h"

#include "emu8950.h" // for opl1/2
#include "opl3.h" // for opl3
#include <inttypes.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

#define convert(value) ((0x000000ff & value) << 24) | ((0x0000ff00 & value) << 8) | \
                       ((0x00ff0000 & value) >> 8) | ((0xff000000 & value) >> 24)

void printf_dim(char string[], u8 bright) {
   printf("\e[22m");
   if (bright==0) printf("\e[2m");
   printf("%s",string);
   if (bright==0) printf("\e[22m");
}

u32 getDW(FILE * file) {
  u32 thing;
  fread( & thing, sizeof(u32), 1, file);
  if (SDL_BYTEORDER == SDL_BIG_ENDIAN) thing = convert(thing);
  return thing;
}

u16 getW(FILE * file) {
  u16 thing = 0;
  fread( & thing, sizeof(u16), 1, file);
  if (SDL_BYTEORDER == SDL_BIG_ENDIAN) thing = convert(thing);
  return thing;
}

char command;
OPL *fm;
opl3_chip fm2;
FILE *vgm;
u8 opl_mode;

u8 opl_reg[512];
u8 op_lut[18] = {0x00, 0x03, 0x01, 0x04, 0x02, 0x05, 0x08, 0x0b, 0x09, 0x0c, 0x0a, 0x0d, 0x10, 0x13, 0x11, 0x14, 0x12, 0x15};
char wave[4][6] = {"[^v^v]", "[^-^-]", "[^^^^]", "[/|/|]"};
char opl3_wave[8][6] = {"[^v^v]", "[^-^-]", "[^^^^]", "[/|/|]","[^v--]","[^^--]","[_-_-]","[////]"};

uint32_t audioSample = 0;
uint32_t lastSample = 0;
uint32_t lastResult = 0;
void audioCallback(void * userdata, unsigned char * stream, int len);

u16 delay;
u32 loop_point;
u32 opl_clock;
u8 has_loop;
u32 eof_off;

int main(int argc, char * argv[]) {
  printf("\e[22m");
  if (argc < 2) {
    printf("USAGE: %s <in>", argv[0]);
    return 0;
  }
  vgm = fopen(argv[1], "rb");
  char header_start[4];
  fgets(header_start, 5, vgm);
  if (strcmp(header_start, "Vgm ") != 0) {
    printf("invalid VGM file");
    return 0;
  }
  printf("\e[1;1H\e[2J");
  printf("header check done\n");

  fseek(vgm, 4, SEEK_SET);
  eof_off = getDW(vgm);
  printf("EOF is %" PRIu32 "\n", eof_off);

  fseek(vgm, 52, SEEK_SET);
  u32 vgm_off;
  vgm_off = getDW(vgm) + 52;
  printf("VGM offset is %" PRIu32 "\n", vgm_off);

  fseek(vgm, 28, SEEK_SET);
  loop_point = getDW(vgm);
  if (loop_point < 28 || loop_point > eof_off) {
    loop_point = vgm_off;
    has_loop = 0;
  } else {
    loop_point -= 28;
    has_loop = 1;
  }
  printf("Loop point is %" PRIu32 "\n", loop_point);

  fseek(vgm, 92, SEEK_SET);
  opl_clock = getDW(vgm);
  if (opl_clock > 0) {
      printf("OPL3 clock speed is %" PRIu32 "hz\n", opl_clock);
      OPL3_Reset(&fm2,44100,opl_clock);
      opl_mode = 3;
  } else {
    fseek(vgm, 80, SEEK_SET);
    opl_clock = getDW(vgm);
    if (opl_clock > 0) {
      printf("OPL2 clock speed is %" PRIu32 "hz\n", opl_clock);
      fm = OPL_new(opl_clock, 44100);
      OPL_reset(fm);
      opl_mode = 2;
      OPL_setChipType(fm, 2);
    } else {
      fseek(vgm, 84, SEEK_SET);
      opl_clock = getDW(vgm);
      if (opl_clock > 0) {
        printf("OPL1 clock speed is %" PRIu32 "hz\n", opl_clock);
        fm = OPL_new(opl_clock, 44100);
        OPL_reset(fm);
        opl_mode = 1;
        OPL_setChipType(fm, 1);
      } else {
       printf("No OPL2 or OPL1 clock speed found");
       return 0;
      }
    }
  }
  for (int i=0;i<256;i++) opl_reg[i] = 0;
  fseek(vgm, vgm_off, SEEK_SET);

  SDL_AudioSpec want;
  SDL_AudioSpec have;
  SDL_AudioDeviceID dev;

  SDL_Init(SDL_INIT_AUDIO);

  SDL_zero(want);
  want.freq = 44100;
  want.format = AUDIO_S16SYS;
  want.channels = 1;
  want.samples = 2048;
  want.callback = audioCallback;
  want.userdata = (void * ) & audioSample;
  dev = SDL_OpenAudioDevice(NULL, 0, & want, & have, 0);

  if (dev == 0) {
    SDL_Log("Failed to open audio: %s", SDL_GetError());
    return 2;
  }

  printf("\nHit CTRL+C to quit\nPlaying %s...\n",argv[1]);
  SDL_PauseAudioDevice(dev, 0);
  while (1) {
     printf("\e[9;0f");
     printf("\n%d/%" PRIu32 "\n",ftell(vgm),eof_off);
     
     if (opl_mode < 3) {
     printf("\n");
     printf_dim("WF ",((opl_reg[0x01]>>5)&1));
     printf_dim("CSM ",((opl_reg[0x01]>>7)&1));
     printf_dim("AMI ",((opl_reg[0xBD]>>7)&1));
     printf_dim("VIBI ",((opl_reg[0xBD]>>6)&1));

     printf_dim("DRUM ",((opl_reg[0xBD]>>5)&1));
     printf_dim("BD ",((opl_reg[0xBD]>>4)&1));
     printf_dim("SD ",((opl_reg[0xBD]>>3)&1));
     printf_dim("TOM ",((opl_reg[0xBD]>>2)&1));
     printf_dim("CYM ",((opl_reg[0xBD]>>1)&1));
     printf_dim("HH ",((opl_reg[0xBD])&1));

     printf_dim("\n                  ml   k   tl    a   d   s   r   o  freq      fbk     wave\n",1);
     for (int ch=0;ch<9;ch++) {
        for (int op=0;op<2;op++) {
          printf("%d ",op+1); 
          u8 op_index = op_lut[op+(ch<<1)]; 
          u8 kon = (opl_reg[ch+0xb0]>>5)&1;
	  printf_dim("AM ",((opl_reg[op_index+0x20]>>7)&1)*kon); // am
	  printf_dim("VIB ",((opl_reg[op_index+0x20]>>6)&1)*kon); // vibrato
	  printf_dim("EGT ",((opl_reg[op_index+0x20]>>5)&1)*kon); // sustain
	  printf_dim("KSR ",((opl_reg[op_index+0x20]>>4)&1)*kon); // key scaling

	  char temp[4];
          sprintf(temp,"[%2d] ",opl_reg[op_index+0x20]&15);
          printf_dim(temp,kon); // mult

          sprintf(temp,"[%d] ",(opl_reg[op_index+0x40]>>6)&3);
          printf_dim(temp,kon); // ksr level

          sprintf(temp,"[%2d] ",(opl_reg[op_index+0x40])&63);
          printf_dim(temp,kon); // op level


          sprintf(temp,"[%2d]",(opl_reg[op_index+0x60]>>4)&15);
          printf_dim(temp,kon); // attack

          sprintf(temp,"[%2d]",(opl_reg[op_index+0x60])&15);
          printf_dim(temp,kon); // decay

          sprintf(temp,"[%2d]",(opl_reg[op_index+0x80]>>4)&15);
          printf_dim(temp,kon); // sustain

          sprintf(temp,"[%2d] ",(opl_reg[op_index+0x80])&15);
          printf_dim(temp,kon); // release


          if (op==1) {
            sprintf(temp,"[%1d]",(opl_reg[ch+0xb0]>>2)&7);
            printf_dim(temp,kon); // octave

            int freq = ((opl_reg[ch+0xb0]&3)<<8)+opl_reg[ch+0xa0];      
            sprintf(temp,"[%4d] ",freq);
            printf_dim(temp,kon); // freq

            printf_dim("KEY ",kon); // key on


            sprintf(temp,"[%d] ",(opl_reg[ch+0xc0]>>1)&7);
            printf_dim(temp,kon); // feedback


	    printf_dim("ALG ",((opl_reg[ch+0xc0])&1)*kon); // algorithm
          } else {
            sprintf(temp,"%d                     ",ch+1);      
            printf_dim(temp,kon);
          }
          sprintf(temp,"%.6s",wave[(opl_reg[op_index+0xe0])&3]);
          printf_dim(temp,kon); // release       
          printf("\n");
        }
     }
     } else {
     printf("\n");
     printf_dim("WF ",((opl_reg[0x01]>>5)&1));
     printf_dim("CSM ",((opl_reg[0x01]>>7)&1));
     printf_dim("AMI ",((opl_reg[0xBD]>>7)&1));
     printf_dim("VIBI ",((opl_reg[0xBD]>>6)&1));

     printf_dim("DRUM ",((opl_reg[0xBD]>>5)&1));
     printf_dim("BD ",((opl_reg[0xBD]>>4)&1));
     printf_dim("SD ",((opl_reg[0xBD]>>3)&1));
     printf_dim("TOM ",((opl_reg[0xBD]>>2)&1));
     printf_dim("CYM ",((opl_reg[0xBD]>>1)&1));
     printf_dim("HH ",((opl_reg[0xBD])&1));

     printf_dim("OPL3 ",((opl_reg[0x105])&1));
     printf_dim("4OP ",(opl_reg[0x104])>0?1:0);

     printf_dim("\n                  ml   k   tl    a   d   s   r   o  freq      fbk     wave\n",1);
     for (int ch=0;ch<9;ch++) {
        for (int op=0;op<2;op++) {
          printf("%d ",op+1); 
          u8 op_index = op_lut[op+(ch<<1)]; 
          u8 kon = (opl_reg[ch+0xb0]>>5)&1;
	  printf_dim("AM ",((opl_reg[op_index+0x20]>>7)&1)*kon); // am
	  printf_dim("VIB ",((opl_reg[op_index+0x20]>>6)&1)*kon); // vibrato
	  printf_dim("EGT ",((opl_reg[op_index+0x20]>>5)&1)*kon); // sustain
	  printf_dim("KSR ",((opl_reg[op_index+0x20]>>4)&1)*kon); // key scaling

	  char temp[4];
          sprintf(temp,"[%2d] ",opl_reg[op_index+0x20]&15);
          printf_dim(temp,kon); // mult

          sprintf(temp,"[%d] ",(opl_reg[op_index+0x40]>>6)&3);
          printf_dim(temp,kon); // ksr level

          sprintf(temp,"[%2d] ",(opl_reg[op_index+0x40])&63);
          printf_dim(temp,kon); // op level


          sprintf(temp,"[%2d]",(opl_reg[op_index+0x60]>>4)&15);
          printf_dim(temp,kon); // attack

          sprintf(temp,"[%2d]",(opl_reg[op_index+0x60])&15);
          printf_dim(temp,kon); // decay

          sprintf(temp,"[%2d]",(opl_reg[op_index+0x80]>>4)&15);
          printf_dim(temp,kon); // sustain

          sprintf(temp,"[%2d] ",(opl_reg[op_index+0x80])&15);
          printf_dim(temp,kon); // release


          if (op==1) {
            sprintf(temp,"[%1d]",(opl_reg[ch+0xb0]>>2)&7);
            printf_dim(temp,kon); // octave

            int freq = ((opl_reg[ch+0xb0]&3)<<8)+opl_reg[ch+0xa0];      
            sprintf(temp,"[%4d] ",freq);
            printf_dim(temp,kon); // freq

            printf_dim("KEY ",kon); // key on


            sprintf(temp,"[%d] ",(opl_reg[ch+0xc0]>>1)&7);
            printf_dim(temp,kon); // feedback


	    printf_dim("ALG ",((opl_reg[ch+0xc0])&1)*kon); // algorithm
          } else {
            sprintf(temp,"%d                     ",ch+1);      
            printf_dim(temp,kon);
          }
          sprintf(temp,"%.6s",opl3_wave[(opl_reg[op_index+0xe0])&7]);
          printf_dim(temp,kon); // release       
          printf("\n");
        }
     }
     for (int ch=0;ch<9;ch++) {
        for (int op=0;op<2;op++) {
          printf("%d ",op+1); 
          u8 op_index = 256+op_lut[op+(ch<<1)]; 
          u8 kon = (opl_reg[ch+256+0xb0]>>5)&1;
	  printf_dim("AM ",((opl_reg[op_index+0x20]>>7)&1)*kon); // am
	  printf_dim("VIB ",((opl_reg[op_index+0x20]>>6)&1)*kon); // vibrato
	  printf_dim("EGT ",((opl_reg[op_index+0x20]>>5)&1)*kon); // sustain
	  printf_dim("KSR ",((opl_reg[op_index+0x20]>>4)&1)*kon); // key scaling

	  char temp[4];
          sprintf(temp,"[%2d] ",opl_reg[op_index+0x20]&15);
          printf_dim(temp,kon); // mult

          sprintf(temp,"[%d] ",(opl_reg[op_index+0x40]>>6)&3);
          printf_dim(temp,kon); // ksr level

          sprintf(temp,"[%2d] ",(opl_reg[op_index+0x40])&63);
          printf_dim(temp,kon); // op level


          sprintf(temp,"[%2d]",(opl_reg[op_index+0x60]>>4)&15);
          printf_dim(temp,kon); // attack

          sprintf(temp,"[%2d]",(opl_reg[op_index+0x60])&15);
          printf_dim(temp,kon); // decay

          sprintf(temp,"[%2d]",(opl_reg[op_index+0x80]>>4)&15);
          printf_dim(temp,kon); // sustain

          sprintf(temp,"[%2d] ",(opl_reg[op_index+0x80])&15);
          printf_dim(temp,kon); // release


          if (op==1) {
            sprintf(temp,"[%1d]",(opl_reg[ch+256+0xb0]>>2)&7);
            printf_dim(temp,kon); // octave

            int freq = ((opl_reg[ch+0xb0]&3)<<8)+opl_reg[ch+256+0xa0];      
            sprintf(temp,"[%4d] ",freq);
            printf_dim(temp,kon); // freq

            printf_dim("KEY ",kon); // key on


            sprintf(temp,"[%d] ",(opl_reg[ch+256+0xc0]>>1)&7);
            printf_dim(temp,kon); // feedback


	    printf_dim("ALG ",((opl_reg[ch+256+0xc0])&1)*kon); // algorithm
          } else {
            sprintf(temp,"%d                    ",ch+1+9);      
            printf_dim(temp,kon);
          }
          sprintf(temp,"%.6s",opl3_wave[(opl_reg[op_index+0xe0])&7]);
          printf_dim(temp,kon); // release       
          printf("\n");
        }
     }
     }
     printf("\e[22m"); 
     SDL_Delay(15);
  }
/*
  SDL_Delay(55000);

  fprintf(stderr, "\n");
  SDL_PauseAudioDevice(dev, 1);
  SDL_CloseAudioDevice(dev);
*/
  return 0;
}

void audioCallback(void * userdata, unsigned char * stream, int len) {
  uint32_t result;
  for (int i = 0; i < len; i += 2) {
    if (delay == 0) {
      command = getc(vgm);
      if (command >> 4 == 0x7) {
        delay = (command & 0xf);
      } else if (command == 0x61) {
        delay = getW(vgm);
      } else if (command == 0x62) {
        delay = 735;
      } else if (command == 0x63) {
        delay = 882;
      }

      // reg write
      else if (command == 0x5e && opl_mode == 3) {
        uint16_t reg = (uint16_t)getc(vgm);
        uint8_t data = getc(vgm);
        OPL3_WriteReg(&fm2,reg, data);  
        opl_reg[reg] = data;   
      } else if (command == 0x5f && opl_mode == 3) {
        uint16_t reg = (uint16_t)getc(vgm);
        uint8_t data = getc(vgm);
        OPL3_WriteReg(&fm2,reg+256, data);  
        opl_reg[reg+256] = data;   
      } 

      else if (command == 0x5a && opl_mode == 2) {
        uint8_t reg = getc(vgm);
        uint8_t data = getc(vgm);
        OPL_writeReg(fm, reg, data);
        opl_reg[reg] = data;
      } else if (command == 0x5b && opl_mode == 1) {
        uint8_t reg = getc(vgm);
        uint8_t data = getc(vgm);
        OPL_writeReg(fm, reg, data);
        opl_reg[reg] = data;
      }
      // eof goto
      else if (command == 0x66 || ftell(vgm) >= eof_off) {
        fseek(vgm, loop_point, SEEK_SET);
        if (has_loop == 0) { // if song doesnt have loop
          OPL_reset(fm);
          OPL3_Reset(&fm2,44100,opl_clock);
          for (int i=0;i<512;i++) opl_reg[i] = 0;
        }
      }
    } else {
      delay--;
    }
    if (opl_mode == 3) {
      int16_t thingaa[2];
      OPL3_GenerateResampled(&fm2,&thingaa);
      result = (uint32_t)((thingaa[0]+thingaa[1])>>1);
    } else {
      result = OPL_calc(fm);
    }
    uint16_t * store = (uint16_t*) & stream[i];
    *store = result;

  }
}