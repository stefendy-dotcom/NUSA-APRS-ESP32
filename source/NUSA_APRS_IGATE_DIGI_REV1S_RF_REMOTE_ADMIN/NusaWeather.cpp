#include "NusaWeather.h"
#include <ctype.h>

static String callText(const AX25Call &c){ String s(c.call); if(c.ssid) s += "-"+String(c.ssid); return s; }
static bool digitsAt(const String&s,int p,int n){ if(p<0||p+n>(int)s.length())return false; for(int i=0;i<n;i++) if(!isdigit((unsigned char)s[p+i]))return false; return true; }
static int findToken(const String&s,char t,int n,bool signed3=false){
  for(int i=0;i<(int)s.length()-n;i++) if(s[i]==t){
    if(signed3 && n==3){ if((s[i+1]=='-'||s[i+1]=='+') && digitsAt(s,i+2,2)) return i; }
    if(digitsAt(s,i+1,n)) return i;
  } return -1;
}
static int intAfter(const String&s,int p,int n){ return s.substring(p+1,p+1+n).toInt(); }
static float hundredthInToMm(int v){ return v * 0.254f; }

bool NusaWeather_parse(const AX25Msg &m, NusaWeatherData &out){
  if(m.len<4) return false;
  String s; s.reserve(m.len); for(size_t i=0;i<m.len;i++){ char c=(char)m.info[i]; if(c && c!='\r'&&c!='\n') s+=c; }
  NusaWeatherData w; int fields=0;
  int p=findToken(s,'c',3); if(p>=0){int v=intAfter(s,p,3);if(v>=0&&v<=360){w.windDirValid=true;w.windDirDeg=v;fields++;}}
  p=findToken(s,'s',3); if(p>=0){int v=intAfter(s,p,3);if(v>=0&&v<=999){w.windSpeedValid=true;w.windSpeedMph=v;fields++;}}
  p=findToken(s,'g',3); if(p>=0){int v=intAfter(s,p,3);if(v>=0&&v<=999){w.gustValid=true;w.gustMph=v;fields++;}}
  p=findToken(s,'t',3,true); if(p>=0){ int f=0; if((s[p+1]=='-'||s[p+1]=='+')) f=s.substring(p+1,p+4).toInt(); else f=intAfter(s,p,3); if(f>=-99&&f<=199){w.tempValid=true;w.tempC=(f-32.0f)*5.0f/9.0f;fields++;} }
  p=findToken(s,'h',2); if(p>=0){int v=intAfter(s,p,2); if(v==0)v=100; if(v>=1&&v<=100){w.humidityValid=true;w.humidity=v;fields++;}}
  p=findToken(s,'b',5); if(p>=0){int v=intAfter(s,p,5); if(v>=8000&&v<=12000){w.pressureValid=true;w.pressureHpa=v/10.0f;fields++;}}
  p=findToken(s,'r',3); if(p>=0){int v=intAfter(s,p,3);w.rain1hValid=true;w.rain1hMm=hundredthInToMm(v);fields++;}
  p=findToken(s,'p',3); if(p>=0){int v=intAfter(s,p,3);w.rain24hValid=true;w.rain24hMm=hundredthInToMm(v);fields++;}
  p=findToken(s,'P',3); if(p>=0){int v=intAfter(s,p,3);w.rainMidnightValid=true;w.rainMidnightMm=hundredthInToMm(v);fields++;}

  bool explicitWeather = s.length() && s[0]=='_';
  if(!explicitWeather && s.length()>19 && (s[0]=='!'||s[0]=='='||s[0]=='/'||s[0]=='@') && s[19]=='_') explicitWeather=true;

  // Common position+weather packets may encode wind as ddd/sss immediately
  // after the weather symbol rather than cddd/sddd tokens.
  if(explicitWeather && (!w.windDirValid || !w.windSpeedValid)) {
    int start = s.indexOf('_'); if(start < 0) start = 0;
    for(int i=start+1; i+6<(int)s.length(); ++i) {
      if(digitsAt(s,i,3) && s[i+3]=='/' && digitsAt(s,i+4,3)) {
        int wd=s.substring(i,i+3).toInt(), ws=s.substring(i+4,i+7).toInt();
        if(wd>=0 && wd<=360 && ws>=0 && ws<=999) {
          if(!w.windDirValid){w.windDirValid=true;w.windDirDeg=wd;fields++;}
          if(!w.windSpeedValid){w.windSpeedValid=true;w.windSpeedMph=ws;fields++;}
          break;
        }
      }
    }
  }
  bool core = w.tempValid||w.humidityValid||w.pressureValid||w.windSpeedValid||w.windDirValid;
  if(!core || (!explicitWeather && fields<2)) return false;
  String src=callText(m.src); strncpy(w.source,src.c_str(),sizeof(w.source)-1); w.source[sizeof(w.source)-1]=0; w.whenMs=millis(); w.valid=true; out=w; return true;
}
