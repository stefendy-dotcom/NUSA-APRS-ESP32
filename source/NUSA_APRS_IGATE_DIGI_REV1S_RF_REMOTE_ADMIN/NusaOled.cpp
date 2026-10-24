#include "NusaOled.h"

// Compact 5x7 uppercase font. Each glyph is 5 columns, bit0 at top.
static const uint8_t G_SPACE[5]={0,0,0,0,0};
static const uint8_t G_QMARK[5]={0x02,0x01,0x59,0x09,0x06};
static const uint8_t G_0[5]={0x3E,0x51,0x49,0x45,0x3E};
static const uint8_t G_1[5]={0x00,0x42,0x7F,0x40,0x00};
static const uint8_t G_2[5]={0x42,0x61,0x51,0x49,0x46};
static const uint8_t G_3[5]={0x21,0x41,0x45,0x4B,0x31};
static const uint8_t G_4[5]={0x18,0x14,0x12,0x7F,0x10};
static const uint8_t G_5[5]={0x27,0x45,0x45,0x45,0x39};
static const uint8_t G_6[5]={0x3C,0x4A,0x49,0x49,0x30};
static const uint8_t G_7[5]={0x01,0x71,0x09,0x05,0x03};
static const uint8_t G_8[5]={0x36,0x49,0x49,0x49,0x36};
static const uint8_t G_9[5]={0x06,0x49,0x49,0x29,0x1E};
static const uint8_t G_A[5]={0x7E,0x11,0x11,0x11,0x7E};
static const uint8_t G_B[5]={0x7F,0x49,0x49,0x49,0x36};
static const uint8_t G_C[5]={0x3E,0x41,0x41,0x41,0x22};
static const uint8_t G_D[5]={0x7F,0x41,0x41,0x22,0x1C};
static const uint8_t G_E[5]={0x7F,0x49,0x49,0x49,0x41};
static const uint8_t G_F[5]={0x7F,0x09,0x09,0x09,0x01};
static const uint8_t G_G[5]={0x3E,0x41,0x49,0x49,0x7A};
static const uint8_t G_H[5]={0x7F,0x08,0x08,0x08,0x7F};
static const uint8_t G_I[5]={0x00,0x41,0x7F,0x41,0x00};
static const uint8_t G_J[5]={0x20,0x40,0x41,0x3F,0x01};
static const uint8_t G_K[5]={0x7F,0x08,0x14,0x22,0x41};
static const uint8_t G_L[5]={0x7F,0x40,0x40,0x40,0x40};
static const uint8_t G_M[5]={0x7F,0x02,0x0C,0x02,0x7F};
static const uint8_t G_N[5]={0x7F,0x04,0x08,0x10,0x7F};
static const uint8_t G_O[5]={0x3E,0x41,0x41,0x41,0x3E};
static const uint8_t G_P[5]={0x7F,0x09,0x09,0x09,0x06};
static const uint8_t G_Q[5]={0x3E,0x41,0x51,0x21,0x5E};
static const uint8_t G_R[5]={0x7F,0x09,0x19,0x29,0x46};
static const uint8_t G_S[5]={0x46,0x49,0x49,0x49,0x31};
static const uint8_t G_T[5]={0x01,0x01,0x7F,0x01,0x01};
static const uint8_t G_U[5]={0x3F,0x40,0x40,0x40,0x3F};
static const uint8_t G_V[5]={0x1F,0x20,0x40,0x20,0x1F};
static const uint8_t G_W[5]={0x3F,0x40,0x38,0x40,0x3F};
static const uint8_t G_X[5]={0x63,0x14,0x08,0x14,0x63};
static const uint8_t G_Y[5]={0x07,0x08,0x70,0x08,0x07};
static const uint8_t G_Z[5]={0x61,0x51,0x49,0x45,0x43};
static const uint8_t G_DASH[5]={0x08,0x08,0x08,0x08,0x08};
static const uint8_t G_DOT[5]={0,0x60,0x60,0,0};
static const uint8_t G_COLON[5]={0,0x36,0x36,0,0};
static const uint8_t G_SLASH[5]={0x20,0x10,0x08,0x04,0x02};
static const uint8_t G_PCT[5]={0x23,0x13,0x08,0x64,0x62};
static const uint8_t G_PLUS[5]={0x08,0x08,0x3E,0x08,0x08};
static const uint8_t G_HASH[5]={0x14,0x7F,0x14,0x7F,0x14};
static const uint8_t G_LP[5]={0,0x1C,0x22,0x41,0};
static const uint8_t G_RP[5]={0,0x41,0x22,0x1C,0};
static const uint8_t G_EQ[5]={0x14,0x14,0x14,0x14,0x14};
static const uint8_t G_LT[5]={0x08,0x14,0x22,0x41,0};
static const uint8_t G_GT[5]={0,0x41,0x22,0x14,0x08};
static const uint8_t G_STAR[5]={0x14,0x08,0x3E,0x08,0x14};

bool NusaOled::probe(uint8_t a){ Wire.beginTransmission(a); return Wire.endTransmission()==0; }
void NusaOled::cmd(uint8_t c){ if(!_ready) return; Wire.beginTransmission(_addr); Wire.write(0x00); Wire.write(c); Wire.endTransmission(); }

bool NusaOled::begin(int sda,int scl,uint8_t address,uint8_t contrast,bool flip,uint8_t controller){
  _ready=false; _controller = (controller==1)?1:2;
  Wire.begin(sda,scl); Wire.setClock(400000); delay(30);
  if(address==0){ if(probe(0x3C)) _addr=0x3C; else if(probe(0x3D)) _addr=0x3D; else return false; }
  else { _addr=address; if(!probe(_addr)) return false; }
  _ready=true; _flip=flip;
  cmd(0xAE);                 // display OFF
  cmd(0xD5); cmd(0x80);      // clock divide
  cmd(0xA8); cmd(0x3F);      // multiplex 1/64
  cmd(0xD3); cmd(0x00);      // display offset
  cmd(0x40);                 // start line 0
  if(_controller==1){
    cmd(0x8D); cmd(0x14);    // SSD1306 charge pump ON
    cmd(0x20); cmd(0x02);    // page addressing mode (common-safe)
  } else {
    cmd(0xAD); cmd(0x8B);    // SH1106 DC-DC ON
  }
  setFlip(flip);
  cmd(0xDA); cmd(0x12);      // COM pins
  cmd(0x81); cmd(contrast);  // contrast
  cmd(0xD9); cmd(_controller==1 ? 0xF1 : 0x22);
  cmd(0xDB); cmd(_controller==1 ? 0x40 : 0x35);
  cmd(0xA4); cmd(0xA6); cmd(0xAF);
  clear(); display(); return true;
}
void NusaOled::end(){ if(_ready) cmd(0xAE); _ready=false; }
void NusaOled::setContrast(uint8_t v){ if(!_ready) return; cmd(0x81); cmd(v); }
void NusaOled::setFlip(bool v){ _flip=v; if(!_ready) return; cmd(v?0xA0:0xA1); cmd(v?0xC0:0xC8); }
void NusaOled::clear(){ memset(_buf,0,sizeof(_buf)); }
void NusaOled::pixel(int x,int y,bool on){ if(x<0||x>=128||y<0||y>=64) return; uint16_t i=x+(y>>3)*128; uint8_t m=1<<(y&7); if(on)_buf[i]|=m; else _buf[i]&=~m; }
void NusaOled::hline(int x,int y,int w,bool on){ for(int i=0;i<w;i++) pixel(x+i,y,on); }
void NusaOled::vline(int x,int y,int h,bool on){ for(int i=0;i<h;i++) pixel(x,y+i,on); }
void NusaOled::rect(int x,int y,int w,int h,bool on){ if(w<=0||h<=0)return; hline(x,y,w,on); hline(x,y+h-1,w,on); vline(x,y,h,on); vline(x+w-1,y,h,on); }
void NusaOled::fillRect(int x,int y,int w,int h,bool on){ for(int yy=0;yy<h;yy++) hline(x,y+yy,w,on); }

const uint8_t* NusaOled::glyph(char c) const{
  if(c>='a'&&c<='z') c-=32;
  if(c>='0'&&c<='9'){ static const uint8_t* d[]={G_0,G_1,G_2,G_3,G_4,G_5,G_6,G_7,G_8,G_9}; return d[c-'0']; }
  if(c>='A'&&c<='Z'){ static const uint8_t* a[]={G_A,G_B,G_C,G_D,G_E,G_F,G_G,G_H,G_I,G_J,G_K,G_L,G_M,G_N,G_O,G_P,G_Q,G_R,G_S,G_T,G_U,G_V,G_W,G_X,G_Y,G_Z}; return a[c-'A']; }
  switch(c){ case ' ':return G_SPACE; case '-':return G_DASH; case '.':return G_DOT; case ':':return G_COLON; case '/':return G_SLASH; case '%':return G_PCT; case '+':return G_PLUS; case '#':return G_HASH; case '(':return G_LP; case ')':return G_RP; case '=':return G_EQ; case '<':return G_LT; case '>':return G_GT; case '*':return G_STAR; default:return G_QMARK; }
}
int NusaOled::textWidth(const String &s,uint8_t scale) const{ return (int)s.length()*6*scale; }
void NusaOled::text(int x,int y,const String &s,uint8_t scale,bool invert){
  if(scale<1) scale=1;
  for(size_t k=0;k<s.length();k++){
    const uint8_t *g=glyph(s[k]);
    for(int col=0;col<6;col++){
      uint8_t bits=(col<5)?g[col]:0;
      for(int row=0;row<8;row++){
        bool p=(row<7)&&((bits>>row)&1);
        if(invert) p=!p;
        if(p) fillRect(x+col*scale,y+row*scale,scale,scale,true);
        else if(invert) fillRect(x+col*scale,y+row*scale,scale,scale,false);
      }
    }
    x += 6*scale; if(x>=128) break;
  }
}
void NusaOled::centered(int y,const String &s,uint8_t scale,bool invert){ int x=(128-textWidth(s,scale))/2; if(x<0)x=0; text(x,y,s,scale,invert); }
void NusaOled::display(){
  if(!_ready) return;
  // Page-by-page transfer works reliably with both SSD1306 and SH1106.
  // SH1106 has 132 RAM columns; visible 128 pixels normally start at column 2.
  const uint8_t colOffset = (_controller==2) ? 2 : 0;
  for(uint8_t page=0; page<8; ++page){
    cmd((uint8_t)(0xB0 | page));
    cmd((uint8_t)(0x00 | (colOffset & 0x0F)));
    cmd((uint8_t)(0x10 | ((colOffset >> 4) & 0x0F)));
    const uint8_t *row = _buf + ((uint16_t)page * 128U);
    for(int x=0; x<128; x+=16){
      Wire.beginTransmission(_addr); Wire.write(0x40); Wire.write(row+x,16);
      if(Wire.endTransmission()!=0){ _ready=false; return; }
    }
  }
}
